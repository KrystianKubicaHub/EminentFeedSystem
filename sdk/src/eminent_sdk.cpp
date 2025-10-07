#include "session_manager.hpp"
#include "transport_layer.hpp"
#include "physical_layer.hpp"
#include "eminent_sdk.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <cctype>
#include <algorithm>

using namespace std;



EminentSdk::EminentSdk(int localPort, const std::string& remoteHost, int remotePort)
    : sessionManager_(outgoingQueue_, *this),
      transportLayer_(sessionManager_.getOutgoingPackages(), sessionManager_),
      codingModule_(transportLayer_.getOutgoingFrames(), transportLayer_),
      physicalLayer_(localPort, remoteHost, remotePort, codingModule_.getOutgoingFrames(), codingModule_),
      localPort_(localPort),
      remoteHost_(remoteHost),
      remotePort_(remotePort)
{
}


void EminentSdk::onMessageReceived(const Message& msg) {
    std::cout << "[EminentSdk] onMessageReceived: ";
    std::cout << "{ id=" << msg.id
              << ", connId=" << msg.connId
              << ", payload='" << msg.payload << "'"
              << ", format=" << static_cast<int>(msg.format)
              << ", priority=" << msg.priority
              << ", requireAck=" << msg.requireAck
              << " }" << std::endl;
    switch (msg.format) {
        case MessageFormat::JSON:
            handleJsonMessage(msg);
            break;
        case MessageFormat::VIDEO:
            handleVideoMessage(msg);
            break;
        case MessageFormat::HANDSHAKE: {
            auto payload = parseHandshakePayload(msg.payload);
            if (!payload.has_value()) {
                std::cout << "[EminentSdk] Failed to parse handshake payload." << std::endl;
                return;
            }
            if (!payload->hasDeviceId || !payload->hasSpecialCode) {
                std::cout << "[EminentSdk] Handshake payload missing required fields." << std::endl;
                return;
            }
            if (payload->hasFinalConfirmation && payload->finalConfirmation) {
                handleHandshakeFinalConfirmation(msg, *payload);
            } else if (payload->hasNewId) {
                handleHandshakeResponse(msg, *payload);
            } else {
                handleHandshakeRequest(msg, *payload);
            }
            break;
        }
        default:
            std::cout << "[EminentSdk] Unknown message format received." << std::endl;
            break;
    }
}

void EminentSdk::handleJsonMessage(const Message& msg) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        auto match = std::find_if(
            connections_.begin(),
            connections_.end(),
            [&](const auto& entry) {
                return entry.second.id == msg.connId;
            }
        );
        if (match == connections_.end()) {
            std::cout << "[EminentSdk] JSON message received for unknown connectionId=" << msg.connId << std::endl;
            return;
        }
        it = match;
    }

    const Connection& conn = it->second;

    auto extractStringField = [](const std::string& json, const std::string& key) -> std::optional<std::string> {
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
        if (json[valueStart] == '"') {
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

        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && json[valueEnd] != ',' && json[valueEnd] != '}' && !std::isspace(static_cast<unsigned char>(json[valueEnd]))) {
            ++valueEnd;
        }
        if (valueEnd <= valueStart) {
            return std::nullopt;
        }
        return json.substr(valueStart, valueEnd - valueStart);
    };

    auto text = extractStringField(msg.payload, "text");
    auto from = extractStringField(msg.payload, "from");

    std::cout << "[EminentSdk] JSON message on connection " << conn.id
              << " (remoteId=" << conn.remoteId << ")" << std::endl;
    if (from.has_value()) {
        std::cout << "  from: " << *from << std::endl;
    }
    if (text.has_value()) {
        std::cout << "  text: " << *text << std::endl;
    } else {
        std::cout << "  payload: " << msg.payload << std::endl;
    }

    if (conn.onMessage) {
        conn.onMessage(msg);
    } else {
        std::cout << "[EminentSdk] No onMessage callback registered for connection " << conn.id << std::endl;
    }
}

void EminentSdk::handleVideoMessage(const Message& msg) {
    std::cout << "[EminentSdk] VIDEO messages are not supported yet. Payload size: " << msg.payload.size() << std::endl;
}

std::string EminentSdk::statusToString(ConnectionStatus status) const {
    switch (status) {
        case ConnectionStatus::PENDING:
            return "PENDING";
        case ConnectionStatus::ACCEPTED:
            return "ACCEPTED";
        case ConnectionStatus::ACTIVE:
            return "ACTIVE";
        case ConnectionStatus::FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

void EminentSdk::complexConsoleInfo(const std::string& title) {
    std::cout << "\n\n";
    if (!title.empty()) {
        std::cout << "========== " << title << " ==========" << std::endl;
    } else {
        std::cout << "========== SDK SUMMARY ==========" << std::endl;
    }

    size_t activeCount = std::count_if(
        connections_.begin(),
        connections_.end(),
        [](const auto& entry) {
            return entry.second.status == ConnectionStatus::ACTIVE;
        }
    );

    std::cout << "Device ID: " << deviceId_ << std::endl;
    std::cout << "Local port: " << localPort_ << std::endl;
    std::cout << "Remote endpoint: " << remoteHost_ << ':' << remotePort_ << std::endl;
    std::cout << "Total connections: " << connections_.size() << std::endl;
    std::cout << "Active connections: " << activeCount << std::endl;

    if (connections_.empty()) {
        std::cout << "(no connections)" << std::endl;
    } else {
        std::cout << "--- Connections ---" << std::endl;
        for (const auto& [cid, conn] : connections_) {
            std::cout << "Connection ID: " << conn.id << std::endl;
            std::cout << "  key: " << cid << std::endl;
            std::cout << "  remoteId: " << conn.remoteId << std::endl;
            std::cout << "  defaultPriority: " << conn.defaultPriority << std::endl;
            std::cout << "  status: " << statusToString(conn.status) << std::endl;
            std::cout << "  specialCode: " << conn.specialCode << std::endl;
            std::cout << "  callbacks: onMessage=" << (conn.onMessage ? "yes" : "no")
                      << ", onTrouble=" << (conn.onTrouble ? "yes" : "no")
                      << ", onDisconnected=" << (conn.onDisconnected ? "yes" : "no")
                      << ", onConnected=" << (conn.onConnected ? "yes" : "no")
                      << std::endl;
        }
    }

    std::cout << "========== END SUMMARY ==========" << std::endl;
    std::cout << "\n\n";
}

void EminentSdk::handleHandshakeRequest(const Message& msg, const HandshakePayload& payload) {
    bool accepted = false;
    if (onIncomingConnectionDecision_) {
        accepted = onIncomingConnectionDecision_(payload.deviceId, msg.payload);
    }
    if (!accepted) {
        std::cout << "[EminentSdk] Handshake from connId=" << msg.connId << " rejected by user decision." << std::endl;
        return;
    }

    std::cout << "[EminentSdk] Handshake from connId=" << msg.connId << " accepted. Generating new connectionId and sending response." << std::endl;

    int myConnId = nextPrime();
    int combinedId = msg.connId * myConnId;

    Connection conn;
    conn.id = combinedId;
    conn.remoteId = payload.deviceId;
    conn.defaultPriority = 0;
    conn.status = ConnectionStatus::ACCEPTED;
    conn.specialCode = payload.specialCode;
    connections_[combinedId] = conn;

    std::cout << "[EminentSdk] Connection " << combinedId << " status set to ACCEPTED." << std::endl;

    MessageId mid = nextMsgId_++;
    std::ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"newId\": " << myConnId << "}";
    std::string respPayload = oss.str();
    Message respMsg{mid, msg.connId, respPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    outgoingQueue_.push(respMsg);
}

void EminentSdk::handleHandshakeResponse(const Message& msg, const HandshakePayload& payload) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        std::cout << "[EminentSdk] Received handshake response for unknown connectionId=" << msg.connId << std::endl;
        return;
    }

    Connection conn = it->second;
    connections_.erase(it);

    int combinedId = msg.connId * payload.newId;
    conn.id = combinedId;
    conn.remoteId = payload.deviceId;
    conn.specialCode = payload.specialCode;
    conn.status = ConnectionStatus::ACTIVE;
    connections_[combinedId] = conn;

    std::cout << "[EminentSdk] Connection " << combinedId << " is now ACTIVE." << std::endl;

    if (conn.onConnected) {
        conn.onConnected(conn.id);
    }

    MessageId ackId = nextMsgId_++;
    std::ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"finalConfirmation\": true}";
    std::string ackPayload = oss.str();
    Message finalAck{ackId, combinedId, ackPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    outgoingQueue_.push(finalAck);
}

void EminentSdk::handleHandshakeFinalConfirmation(const Message& msg, const HandshakePayload& payload) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        std::cout << "[EminentSdk] Received final confirmation for unknown connectionId=" << msg.connId << std::endl;
        return;
    }

    Connection& conn = it->second;
    conn.remoteId = payload.deviceId;
    conn.specialCode = payload.specialCode;
    bool wasActive = (conn.status == ConnectionStatus::ACTIVE);
    conn.status = ConnectionStatus::ACTIVE;

    if (!wasActive) {
        std::cout << "[EminentSdk] Connection " << conn.id << " marked ACTIVE after final confirmation." << std::endl;
        if (onConnectionEstablished_) {
            onConnectionEstablished_(conn.id, conn.remoteId);
        }
    }

    if (conn.onConnected) {
        conn.onConnected(conn.id);
    }
}

std::optional<EminentSdk::HandshakePayload> EminentSdk::parseHandshakePayload(const std::string& payload) {
    HandshakePayload result;

    auto parseIntField = [&](const std::string& key) -> std::optional<int> {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = payload.find(token);
        if (keyPos == std::string::npos) {
            return std::nullopt;
        }
        size_t colon = payload.find(":", keyPos + token.size());
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        size_t valueStart = colon + 1;
        while (valueStart < payload.size() && std::isspace(static_cast<unsigned char>(payload[valueStart]))) {
            ++valueStart;
        }
        size_t valueEnd = valueStart;
        while (valueEnd < payload.size() && (std::isdigit(static_cast<unsigned char>(payload[valueEnd])) || payload[valueEnd] == '-')) {
            ++valueEnd;
        }
        if (valueStart == valueEnd) {
            return std::nullopt;
        }
        try {
            int value = std::stoi(payload.substr(valueStart, valueEnd - valueStart));
            return value;
        } catch (...) {
            return std::nullopt;
        }
    };

    auto parseBoolField = [&](const std::string& key) -> std::optional<bool> {
        const std::string token = "\"" + key + "\"";
        size_t keyPos = payload.find(token);
        if (keyPos == std::string::npos) {
            return std::nullopt;
        }
        size_t colon = payload.find(":", keyPos + token.size());
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        size_t valueStart = colon + 1;
        while (valueStart < payload.size() && std::isspace(static_cast<unsigned char>(payload[valueStart]))) {
            ++valueStart;
        }
        if (payload.compare(valueStart, 4, "true") == 0) {
            return true;
        }
        if (payload.compare(valueStart, 5, "false") == 0) {
            return false;
        }
        return std::nullopt;
    };

    if (auto val = parseIntField("deviceId")) {
        result.hasDeviceId = true;
        result.deviceId = *val;
    }
    if (auto val = parseIntField("specialCode")) {
        result.hasSpecialCode = true;
        result.specialCode = *val;
    }
    if (auto val = parseIntField("newId")) {
        result.hasNewId = true;
        result.newId = *val;
    }
    if (auto val = parseBoolField("finalConfirmation")) {
        result.hasFinalConfirmation = true;
        result.finalConfirmation = *val;
    }

    if (!result.hasDeviceId && !result.hasSpecialCode && !result.hasNewId) {
        return std::nullopt;
    }

    return result;
}

void EminentSdk::initialize(
    DeviceId selfId,
    function<void()> onSuccess,
    function<void(const string&)> onFailure,
    function<bool(DeviceId, const std::string& payload)> onIncomingConnectionDecision,
    function<void(ConnectionId, DeviceId)> onConnectionEstablished 
) {
    if (initialized_) {
        if (onFailure) {
            onFailure("SDK already initialized");
        }
        return;
    }
    deviceId_ = selfId;

    onIncomingConnectionDecision_ = onIncomingConnectionDecision;
    onConnectionEstablished_ = onConnectionEstablished; 
   
    initialized_ = true;
    cout << "SDK initialized for device: " << selfId << endl;
    if (onSuccess) {
        onSuccess();
    }
}

int EminentSdk::nextPrime() {
    int candidate = nextConnectionId_;
    while (true) {
        bool isPrime = candidate > 1;
        for (int i = 2; i * i <= candidate; ++i) {
            if (candidate % i == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) {
            nextConnectionId_ = candidate + 1;
            return candidate;
        }
        ++candidate;
    }
}

void EminentSdk::connect(
    DeviceId targetId,
    Priority defaultPriority,
    function<void(ConnectionId)> onSuccess,
    function<void(const string&)> onFailure,
    function<void(const string&)> onTrouble,
    function<void()> onDisconnected,
    function<void(ConnectionId)> onConnected,
    function<void(const Message&)> onMessage
) {
    if (targetId <= 0) {
        if (onFailure) {
            onFailure("Invalid target ID");
        }
        return;
    }

    ConnectionId cid = nextPrime();
    Connection conn;
    conn.id = cid;
    conn.remoteId = targetId;
    conn.defaultPriority = defaultPriority;
    conn.onMessage = onMessage;
    conn.onTrouble = onTrouble;
    conn.onDisconnected = onDisconnected;
    conn.onConnected = onConnected;
    conn.status = ConnectionStatus::PENDING;
    // Losowy klucz
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(100000, 999999);
    conn.specialCode = dist(gen);
    connections_[cid] = conn;

    MessageId mid = nextMsgId_++;
    // Serializacja JSON handshake
    std::ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << "}";
    string payload = oss.str();
    Message handshakeMsg{mid, cid, payload, MessageFormat::HANDSHAKE, defaultPriority, true, [onSuccess, cid]() { if (onSuccess) onSuccess(cid); }};
    outgoingQueue_.push(handshakeMsg);

    cout << "trying to connect to device " << targetId << " with connection ID " << cid << endl;
}

void EminentSdk::close(ConnectionId id) {
    if (connections_.count(id)) {
        if (connections_[id].onDisconnected) {
            connections_[id].onDisconnected();
        }
        connections_.erase(id);
        cout << "Connection " << id << " closed." << endl;
    }
}

void EminentSdk::send(
    ConnectionId id,
    const string& payload,
    MessageFormat format,
    Priority priority,
    bool requireAck,
    function<void()> onDelivered
) {
    if (!connections_.count(id)) {
        throw std::runtime_error("Send failed: invalid connection ID.");
    }
    if (connections_[id].status == ConnectionStatus::PENDING) {
        throw std::runtime_error("Send failed: connection is still pending.");
    }

    MessageId mid = nextMsgId_++;
    Message msg{ mid, id, payload, format, priority, requireAck, onDelivered };
    outgoingQueue_.push(msg);

    cout << "Message queued with ID " << mid << " on connection " << id << endl;
}

void EminentSdk::setDefaultPriority(ConnectionId id, Priority priority) {
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        auto match = std::find_if(
            connections_.begin(),
            connections_.end(),
            [id](const auto& entry) {
                return entry.second.id == id;
            }
        );
        if (match == connections_.end()) {
            std::cout << "[EminentSdk] setDefaultPriority: connection " << id << " not found." << std::endl;
            return;
        }
        it = match;
    }

    it->second.defaultPriority = priority;
    std::cout << "[EminentSdk] Connection " << it->second.id
              << " default priority set to " << priority << std::endl;
}

void EminentSdk::setOnMessageHandler(ConnectionId id, std::function<void(const Message&)> handler) {
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        auto match = std::find_if(
            connections_.begin(),
            connections_.end(),
            [id](const auto& entry) {
                return entry.second.id == id;
            }
        );
        if (match == connections_.end()) {
            std::cout << "[EminentSdk] setOnMessageHandler: connection " << id << " not found." << std::endl;
            return;
        }
        it = match;
    }

    it->second.onMessage = std::move(handler);
    std::cout << "[EminentSdk] Connection " << it->second.id
              << " onMessage handler updated." << std::endl;
}

void EminentSdk::getStats(
    function<void(const vector<ConnectionStats>&)> onStats,
    ConnectionId id
) {
    vector<ConnectionStats> stats;
    if (id == -1) {
        for (auto& [cid, conn] : connections_) {
            stats.push_back({cid, 10.0, 0.1, 5.0, 0});
        }
    } else if (connections_.count(id)) {
        stats.push_back({id, 12.0, 0.05, 6.0, 0});
    }
    onStats(stats);
}
