#include "EminentSdk.hpp"
#include <atomic>
#include <chrono>
#include <cctype>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMessageCountPerDirection = 200;
constexpr int kMaxMessageLength = 64;
constexpr int kDefaultPriorityA = 6;
constexpr int kDefaultPriorityB = 4;
constexpr int kMaxDebugRecords = 10;

struct DecodedPayload {
    std::string from;
    std::string text;
    int index = -1;
};

struct ExpectedMessage {
    std::string payload;
    std::string text;
};

struct UnexpectedRecord {
    int index;
    std::string payload;
};

struct MismatchRecord {
    int index;
    std::string expectedText;
    std::string receivedText;
};

struct DirectionState {
    std::unordered_map<int, ExpectedMessage> expectedMessages;
    std::vector<UnexpectedRecord> unexpectedMessages;
    std::vector<MismatchRecord> mismatchedMessages;
    int unexpectedTotal = 0;
    int mismatchedTotal = 0;
    int parseFailures = 0;
};

std::string generateRandomString(std::mt19937& engine,
                                 std::uniform_int_distribution<int>& lengthDist,
                                 const std::string& alphabet,
                                 std::uniform_int_distribution<int>& charDist) {
    const int len = lengthDist(engine);
    std::string result;
    result.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        result.push_back(alphabet[static_cast<size_t>(charDist(engine))]);
    }
    return result;
}

std::string buildJsonPayload(const std::string& from,
                             const std::string& randomText,
                             int index) {
    std::string payload = "{\"from\":\"";
    payload += from;
    payload += "\",\"text\":\"";
    payload += randomText;
    payload += "\",\"index\":";
    payload += std::to_string(index);
    payload += "}";
    return payload;
}

std::optional<std::string> extractStringField(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t keyPos = json.find(token);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon = json.find(":", keyPos + token.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    size_t valueStart = colon + 1;
    while (valueStart < json.size() && std::isspace(static_cast<unsigned char>(json[valueStart]))) {
        ++valueStart;
    }
    if (valueStart >= json.size() || json[valueStart] != '"') {
        return std::nullopt;
    }
    ++valueStart;
    std::string result;
    bool escape = false;
    for (size_t i = valueStart; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            result.push_back(c);
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            return result;
        } else {
            result.push_back(c);
        }
    }
    return std::nullopt;
}

std::optional<int> extractIntField(const std::string& json, const std::string& key) {
    const std::string token = "\"" + key + "\"";
    size_t keyPos = json.find(token);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }
    size_t colon = json.find(":", keyPos + token.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    size_t valueStart = colon + 1;
    while (valueStart < json.size() && std::isspace(static_cast<unsigned char>(json[valueStart]))) {
        ++valueStart;
    }
    if (valueStart >= json.size()) {
        return std::nullopt;
    }
    bool negative = false;
    if (json[valueStart] == '-') {
        negative = true;
        ++valueStart;
    }
    if (valueStart >= json.size() || !std::isdigit(static_cast<unsigned char>(json[valueStart]))) {
        return std::nullopt;
    }
    size_t valueEnd = valueStart;
    while (valueEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[valueEnd]))) {
        ++valueEnd;
    }
    std::string numberStr = json.substr(valueStart, valueEnd - valueStart);
    try {
        int value = std::stoi(numberStr);
        return negative ? -value : value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<DecodedPayload> decodePayload(const std::string& json) {
    auto from = extractStringField(json, "from");
    auto text = extractStringField(json, "text");
    auto index = extractIntField(json, "index");
    if (!text.has_value() || !index.has_value()) {
        return std::nullopt;
    }
    DecodedPayload decoded;
    decoded.from = from.value_or("");
    decoded.text = *text;
    decoded.index = *index;
    return decoded;
}

void verifyMessage(const std::string& payload,
                   DirectionState& state,
                   std::mutex& mtx,
                   std::atomic<int>& correct,
                   std::atomic<int>& incorrect) {
    auto decoded = decodePayload(payload);
    if (!decoded.has_value()) {
        ++incorrect;
        std::lock_guard<std::mutex> lock(mtx);
        ++state.parseFailures;
        ++state.unexpectedTotal;
        if (state.unexpectedMessages.size() < kMaxDebugRecords) {
            state.unexpectedMessages.push_back({-1, payload});
        }
        return;
    }

    std::lock_guard<std::mutex> lock(mtx);
    auto it = state.expectedMessages.find(decoded->index);
    if (it == state.expectedMessages.end()) {
        ++incorrect;
        ++state.unexpectedTotal;
        if (state.unexpectedMessages.size() < kMaxDebugRecords) {
            state.unexpectedMessages.push_back({decoded->index, payload});
        }
        return;
    }

    if (it->second.text == decoded->text) {
        ++correct;
    } else {
        ++incorrect;
        ++state.mismatchedTotal;
        if (state.mismatchedMessages.size() < kMaxDebugRecords) {
            state.mismatchedMessages.push_back({decoded->index, it->second.text, decoded->text});
        }
    }
    state.expectedMessages.erase(it);
}

void printDebugDetails(const std::string& label,
                       const DirectionState& state,
                       int totalExpected) {
    std::cout << "\n--- Szczegoly debugowe dla kierunku " << label << " ---" << std::endl;
    std::cout << "  Pozostale oczekiwane: " << state.expectedMessages.size()
              << " / " << totalExpected << std::endl;
    if (!state.expectedMessages.empty()) {
        std::cout << "  Przykladowe brakujace wiadomosci:" << std::endl;
        int count = 0;
        for (const auto& entry : state.expectedMessages) {
            std::cout << "    index=" << entry.first
                      << ", payload='" << entry.second.payload << "'" << std::endl;
            if (++count >= kMaxDebugRecords) {
                break;
            }
        }
    }

    std::cout << "  Liczba niezgodnych: " << state.mismatchedTotal << std::endl;
    if (!state.mismatchedMessages.empty()) {
        std::cout << "  Przykladowe niezgodnosci:" << std::endl;
        for (const auto& mismatch : state.mismatchedMessages) {
            std::cout << "    index=" << mismatch.index
                      << ", expected='" << mismatch.expectedText
                      << "', received='" << mismatch.receivedText << "'" << std::endl;
        }
    }

    std::cout << "  Niespodziewane/duplikaty: " << state.unexpectedTotal << std::endl;
    if (!state.unexpectedMessages.empty()) {
        std::cout << "  Przykladowe niespodziewane (index=-1 oznacza blad parsowania):" << std::endl;
        for (const auto& unexpected : state.unexpectedMessages) {
            std::cout << "    index=" << unexpected.index
                      << ", payload='" << unexpected.payload << "'" << std::endl;
        }
    }

    if (state.parseFailures > 0) {
        std::cout << "  Nieudane parsowania: " << state.parseFailures << std::endl;
    }
}

} // namespace

int main() {
    std::cout << "[Test] Start - test_stabilnosci_2_synchroniczny" << std::endl;

    EminentSdk sdkA(8101, "127.0.0.1", 8102);
    EminentSdk sdkB(8102, "127.0.0.1", 8101);

    const DeviceId deviceA = 3001;
    const DeviceId deviceB = 4002;

    bool initA = false;
    bool initB = false;
    ConnectionId connectionIdA = -1;
    ConnectionId connectionIdB = -1;

    std::atomic<bool> onConnectedA{false};
    std::atomic<bool> onConnectedB{false};

    std::atomic<int> correctToA{0};
    std::atomic<int> incorrectToA{0};
    std::atomic<int> correctToB{0};
    std::atomic<int> incorrectToB{0};

    DirectionState directionAtoB;
    DirectionState directionBtoA;
    std::mutex mutexAtoB;
    std::mutex mutexBtoA;

    sdkA.initialize(
        deviceA,
        [&]() {
            std::cout << "sdkA initialized" << std::endl;
            initA = true;
        },
        [&](const std::string& err) {
            std::cout << "sdkA initialization failed: " << err << std::endl;
        },
        [](DeviceId remoteId, const std::string& payload) {
            std::cout << "sdkA handshake from " << remoteId << ", payload='" << payload << "' -> ACCEPT" << std::endl;
            return true;
        }
    );

    sdkB.initialize(
        deviceB,
        [&]() {
            std::cout << "sdkB initialized" << std::endl;
            initB = true;
        },
        [&](const std::string& err) {
            std::cout << "sdkB initialization failed: " << err << std::endl;
        },
        [](DeviceId remoteId, const std::string& payload) {
            std::cout << "sdkB handshake from " << remoteId << ", payload='" << payload << "' -> ACCEPT" << std::endl;
            return true;
        },
        [&](ConnectionId cid, DeviceId remoteId) {
            connectionIdB = cid;
            onConnectedB = true;
            std::cout << "sdkB connected with device " << remoteId << ", connection id " << cid << std::endl;
            sdkB.setOnMessageHandler(cid, [&](const Message& msg) {
                verifyMessage(msg.payload, directionAtoB, mutexAtoB, correctToB, incorrectToB);
            });
        }
    );

    const auto initStart = std::chrono::steady_clock::now();
    while ((!initA || !initB) && std::chrono::steady_clock::now() - initStart < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!initA || !initB) {
        std::cout << "Initialization timeout" << std::endl;
        return 1;
    }

    sdkA.connect(
        deviceB,
        5,
        [&](ConnectionId cid) {
            std::cout << "sdkA connect success, connection id: " << cid << std::endl;
        },
        [&](const std::string& err) {
            std::cout << "sdkA connect failed: " << err << std::endl;
        },
        [&](const std::string& trouble) {
            std::cout << "sdkA trouble: " << trouble << std::endl;
        },
        [&]() {
            std::cout << "sdkA disconnected" << std::endl;
        },
        [&](ConnectionId cid) {
            connectionIdA = cid;
            onConnectedA = true;
            std::cout << "sdkA onConnected: final connection id " << cid << std::endl;
            sdkA.setOnMessageHandler(cid, [&](const Message& msg) {
                verifyMessage(msg.payload, directionBtoA, mutexBtoA, correctToA, incorrectToA);
            });
        },
        [](const Message& msg) {
            std::cout << "sdkA default handler received message id " << msg.id << std::endl;
        }
    );

    const auto connectStart = std::chrono::steady_clock::now();
    while ((connectionIdA == -1 || connectionIdB == -1) &&
           std::chrono::steady_clock::now() - connectStart < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (connectionIdA == -1 || connectionIdB == -1) {
        std::cout << "Connection establishment timeout" << std::endl;
        return 2;
    }

    sdkA.setDefaultPriority(connectionIdA, kDefaultPriorityA);
    sdkB.setDefaultPriority(connectionIdB, kDefaultPriorityB);

    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_int_distribution<int> lengthDist(1, kMaxMessageLength);
    const std::string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<int> charDist(0, static_cast<int>(alphabet.size()) - 1);

    int sendFailuresA = 0;
    int sendFailuresB = 0;

    for (int i = 0; i < kMessageCountPerDirection; ++i) {
        const std::string randomTextA = generateRandomString(engine, lengthDist, alphabet, charDist);
        const std::string payloadA = buildJsonPayload(std::to_string(deviceA), randomTextA, i);
        {
            std::lock_guard<std::mutex> lock(mutexAtoB);
            directionAtoB.expectedMessages[i] = {payloadA, randomTextA};
        }
        try {
            sdkA.send(
                connectionIdA,
                payloadA,
                MessageFormat::JSON,
                kDefaultPriorityA,
                false,
                nullptr
            );
        } catch (const std::exception& ex) {
            ++sendFailuresA;
            std::cout << "sdkA send failed: " << ex.what() << std::endl;
            std::lock_guard<std::mutex> lock(mutexAtoB);
            directionAtoB.expectedMessages.erase(i);
        }

        const std::string randomTextB = generateRandomString(engine, lengthDist, alphabet, charDist);
        const std::string payloadB = buildJsonPayload(std::to_string(deviceB), randomTextB, i);
        {
            std::lock_guard<std::mutex> lock(mutexBtoA);
            directionBtoA.expectedMessages[i] = {payloadB, randomTextB};
        }
        try {
            sdkB.send(
                connectionIdB,
                payloadB,
                MessageFormat::JSON,
                kDefaultPriorityB,
                false,
                nullptr
            );
        } catch (const std::exception& ex) {
            ++sendFailuresB;
            std::cout << "sdkB send failed: " << ex.what() << std::endl;
            std::lock_guard<std::mutex> lock(mutexBtoA);
            directionBtoA.expectedMessages.erase(i);
        }
    }

    const auto waitStart = std::chrono::steady_clock::now();
    const auto maxWait = std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() - waitStart < maxWait) {
        bool pendingAtoB = false;
        bool pendingBtoA = false;
        {
            std::lock_guard<std::mutex> lock(mutexAtoB);
            pendingAtoB = !directionAtoB.expectedMessages.empty();
        }
        {
            std::lock_guard<std::mutex> lock(mutexBtoA);
            pendingBtoA = !directionBtoA.expectedMessages.empty();
        }
        if (!pendingAtoB && !pendingBtoA) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    DirectionState snapshotAtoB;
    DirectionState snapshotBtoA;
    {
        std::lock_guard<std::mutex> lock(mutexAtoB);
        snapshotAtoB = directionAtoB;
    }
    {
        std::lock_guard<std::mutex> lock(mutexBtoA);
        snapshotBtoA = directionBtoA;
    }

    const int finalCorrectToA = correctToA.load();
    const int finalIncorrectToA = incorrectToA.load();
    const int finalCorrectToB = correctToB.load();
    const int finalIncorrectToB = incorrectToB.load();

    const int missingToB = static_cast<int>(snapshotAtoB.expectedMessages.size());
    const int missingToA = static_cast<int>(snapshotBtoA.expectedMessages.size());

    std::cout << "\n=== Podsumowanie testu stabilnosci 2 synchronicznego ===" << std::endl;
    std::cout << "Wiadomosci wyslane z A do B: " << kMessageCountPerDirection << std::endl;
    std::cout << "  Odebrane poprawnie przez B: " << finalCorrectToB << std::endl;
    std::cout << "  Bledne u B: " << finalIncorrectToB << std::endl;
    std::cout << "  Brakujace u B: " << missingToB << std::endl;
    std::cout << "  Niepowodzenia wysylki A: " << sendFailuresA << std::endl;

    std::cout << "Wiadomosci wyslane z B do A: " << kMessageCountPerDirection << std::endl;
    std::cout << "  Odebrane poprawnie przez A: " << finalCorrectToA << std::endl;
    std::cout << "  Bledne u A: " << finalIncorrectToA << std::endl;
    std::cout << "  Brakujace u A: " << missingToA << std::endl;
    std::cout << "  Niepowodzenia wysylki B: " << sendFailuresB << std::endl;

    printDebugDetails("A -> B (B odbiera)", snapshotAtoB, kMessageCountPerDirection);
    printDebugDetails("B -> A (A odbiera)", snapshotBtoA, kMessageCountPerDirection);

    std::cout << "[Test] Zakonczono test stabilnosci 2 synchroniczny" << std::endl;

    return 0;
}
