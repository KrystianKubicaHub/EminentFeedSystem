#include "eminent_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

using namespace std;



EminentSdk::EminentSdk(int localPort, const string& remoteHost, int remotePort)
        : LoggerBase("EminentSdk"),
            sessionManager_(outgoingQueue_, *this),
      transportLayer_(sessionManager_.getOutgoingPackages(), sessionManager_),
      codingModule_(transportLayer_.getOutgoingFrames(), transportLayer_),
      physicalLayer_(localPort, remoteHost, remotePort, codingModule_.getOutgoingFrames(), codingModule_),
      localPort_(localPort),
      remoteHost_(remoteHost),
      remotePort_(remotePort)
{
}


void EminentSdk::onMessageReceived(const Message& msg) {
    ostringstream oss;
    oss << "onMessageReceived id=" << msg.id
        << " connId=" << msg.connId
        << " payload='" << msg.payload << "'"
        << " format=" << static_cast<int>(msg.format)
        << " priority=" << msg.priority
        << " requireAck=" << msg.requireAck;
    log(LogLevel::INFO, oss.str());
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
                log(LogLevel::WARN, string("Failed to parse handshake payload: '") + msg.payload + "'");
                return;
            }
            if (!payload->hasDeviceId || !payload->hasSpecialCode) {
                log(LogLevel::WARN, "Handshake payload missing required fields");
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
            log(LogLevel::WARN, string("Unknown message format: ") + to_string(static_cast<int>(msg.format)));
            break;
    }
}

void EminentSdk::handleJsonMessage(const Message& msg) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        auto match = find_if(
            connections_.begin(),
            connections_.end(),
            [&](const auto& entry) {
                return entry.second.id == msg.connId;
            }
        );
        if (match == connections_.end()) {
            log(LogLevel::WARN, string("JSON message for unknown connectionId=") + to_string(msg.connId));
            return;
        }
        it = match;
    }

    const Connection& conn = it->second;

    auto extractStringField = [](const string& json, const string& key) -> optional<string> {
        const string token = "\"" + key + "\"";
        size_t keyPos = json.find(token);
        if (keyPos == string::npos) {
            return nullopt;
        }
        size_t colon = json.find(":", keyPos + token.size());
        if (colon == string::npos) {
            return nullopt;
        }
        size_t valueStart = colon + 1;
        while (valueStart < json.size() && isspace(static_cast<unsigned char>(json[valueStart]))) {
            ++valueStart;
        }
        if (valueStart >= json.size()) {
            return nullopt;
        }
        if (json[valueStart] == '"') {
            ++valueStart;
            string result;
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
            return nullopt;
        }

        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && json[valueEnd] != ',' && json[valueEnd] != '}' && !isspace(static_cast<unsigned char>(json[valueEnd]))) {
            ++valueEnd;
        }
        if (valueEnd <= valueStart) {
            return nullopt;
        }
        return json.substr(valueStart, valueEnd - valueStart);
    };

    auto text = extractStringField(msg.payload, "text");
    auto from = extractStringField(msg.payload, "from");

    ostringstream oss;
    oss << "JSON message on connection " << conn.id << " remoteId=" << conn.remoteId;
    if (from.has_value()) {
        oss << " from=" << *from;
    }
    if (text.has_value()) {
        oss << " text='" << *text << "'";
    } else {
        oss << " payload='" << msg.payload << "'";
    }
    log(LogLevel::INFO, oss.str());

    if (conn.onMessage) {
        conn.onMessage(msg);
    } else {
        log(LogLevel::WARN, string("No onMessage callback for connection ") + to_string(conn.id));
    }
}

void EminentSdk::handleVideoMessage(const Message& msg) {
    log(LogLevel::INFO, string("VIDEO messages not supported; payload size=") + to_string(msg.payload.size()));
}

string EminentSdk::statusToString(ConnectionStatus status) const {
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

void EminentSdk::complexConsoleInfo(const string& title) {
    ostringstream summary;
    summary << "\n\n";
    if (!title.empty()) {
        summary << "========== " << title << " ==========" << '\n';
    } else {
        summary << "========== SDK SUMMARY ==========" << '\n';
    }

    size_t activeCount = count_if(
        connections_.begin(),
        connections_.end(),
        [](const auto& entry) {
            return entry.second.status == ConnectionStatus::ACTIVE;
        }
    );

    summary << "Device ID: " << deviceId_ << '\n';
    summary << "Local port: " << localPort_ << '\n';
    summary << "Remote endpoint: " << remoteHost_ << ':' << remotePort_ << '\n';
    summary << "Total connections: " << connections_.size() << '\n';
    summary << "Active connections: " << activeCount << '\n';

    if (connections_.empty()) {
        summary << "(no connections)\n";
    } else {
        summary << "--- Connections ---\n";
        for (const auto& [cid, conn] : connections_) {
            summary << "Connection ID: " << conn.id << '\n';
            summary << "  key: " << cid << '\n';
            summary << "  remoteId: " << conn.remoteId << '\n';
            summary << "  defaultPriority: " << conn.defaultPriority << '\n';
            summary << "  status: " << statusToString(conn.status) << '\n';
            summary << "  specialCode: " << conn.specialCode << '\n';
            summary << "  callbacks: onMessage=" << (conn.onMessage ? "yes" : "no")
                    << ", onTrouble=" << (conn.onTrouble ? "yes" : "no")
                    << ", onDisconnected=" << (conn.onDisconnected ? "yes" : "no")
                    << ", onConnected=" << (conn.onConnected ? "yes" : "no") << '\n';
        }
    }

    summary << "========== END SUMMARY ==========" << "\n\n";
    log(LogLevel::INFO, summary.str());
}

void EminentSdk::handleHandshakeRequest(const Message& msg, const HandshakePayload& payload) {
    bool accepted = false;
    if (onIncomingConnectionDecision_) {
        accepted = onIncomingConnectionDecision_(payload.deviceId, msg.payload);
    }
    if (!accepted) {
        log(LogLevel::INFO, string("Handshake connId=") + to_string(msg.connId) + " rejected by decision");
        return;
    }

    log(LogLevel::INFO, string("Handshake connId=") + to_string(msg.connId) + " accepted -> sending response");

    int myConnId = nextPrime();
    int combinedId = msg.connId * myConnId;

    Connection conn;
    conn.id = combinedId;
    conn.remoteId = payload.deviceId;
    conn.defaultPriority = 0;
    conn.status = ConnectionStatus::ACCEPTED;
    conn.specialCode = payload.specialCode;
    connections_[combinedId] = conn;

    log(LogLevel::INFO, string("Connection ") + to_string(combinedId) + " status set to ACCEPTED");

    MessageId mid = nextMsgId_++;
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"newId\": " << myConnId << "}";
    string respPayload = oss.str();
    Message respMsg{mid, msg.connId, respPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    outgoingQueue_.push(respMsg);
}

void EminentSdk::handleHandshakeResponse(const Message& msg, const HandshakePayload& payload) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("Handshake response for unknown connectionId=") + to_string(msg.connId));
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

    log(LogLevel::INFO, string("Connection ") + to_string(combinedId) + " is now ACTIVE");

    if (conn.onConnected) {
        conn.onConnected(conn.id);
    }

    MessageId ackId = nextMsgId_++;
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"finalConfirmation\": true}";
    string ackPayload = oss.str();
    Message finalAck{ackId, combinedId, ackPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    outgoingQueue_.push(finalAck);
}

void EminentSdk::handleHandshakeFinalConfirmation(const Message& msg, const HandshakePayload& payload) {
    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("Final confirmation for unknown connectionId=") + to_string(msg.connId));
        return;
    }

    Connection& conn = it->second;
    conn.remoteId = payload.deviceId;
    conn.specialCode = payload.specialCode;
    bool wasActive = (conn.status == ConnectionStatus::ACTIVE);
    conn.status = ConnectionStatus::ACTIVE;

    if (!wasActive) {
        log(LogLevel::INFO, string("Connection ") + to_string(conn.id) + " marked ACTIVE after final confirmation");
        if (onConnectionEstablished_) {
            onConnectionEstablished_(conn.id, conn.remoteId);
        }
    }

    if (conn.onConnected) {
        conn.onConnected(conn.id);
    }
}

optional<EminentSdk::HandshakePayload> EminentSdk::parseHandshakePayload(const string& payload) {
    HandshakePayload result;

    auto parseIntField = [&](const string& key) -> optional<int> {
        const string token = "\"" + key + "\"";
        size_t keyPos = payload.find(token);
        if (keyPos == string::npos) {
            return nullopt;
        }
        size_t colon = payload.find(":", keyPos + token.size());
        if (colon == string::npos) {
            return nullopt;
        }
        size_t valueStart = colon + 1;
        while (valueStart < payload.size() && isspace(static_cast<unsigned char>(payload[valueStart]))) {
            ++valueStart;
        }
        size_t valueEnd = valueStart;
        while (valueEnd < payload.size() && (isdigit(static_cast<unsigned char>(payload[valueEnd])) || payload[valueEnd] == '-')) {
            ++valueEnd;
        }
        if (valueStart == valueEnd) {
            return nullopt;
        }
        try {
            int value = stoi(payload.substr(valueStart, valueEnd - valueStart));
            return value;
        } catch (...) {
            return nullopt;
        }
    };

    auto parseBoolField = [&](const string& key) -> optional<bool> {
        const string token = "\"" + key + "\"";
        size_t keyPos = payload.find(token);
        if (keyPos == string::npos) {
            return nullopt;
        }
        size_t colon = payload.find(":", keyPos + token.size());
        if (colon == string::npos) {
            return nullopt;
        }
        size_t valueStart = colon + 1;
        while (valueStart < payload.size() && isspace(static_cast<unsigned char>(payload[valueStart]))) {
            ++valueStart;
        }
        if (payload.compare(valueStart, 4, "true") == 0) {
            return true;
        }
        if (payload.compare(valueStart, 5, "false") == 0) {
            return false;
        }
        return nullopt;
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
        return nullopt;
    }

    return result;
}

void EminentSdk::initialize(
    DeviceId selfId,
    function<void()> onSuccess,
    function<void(const string&)> onFailure,
    function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision,
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
    log(LogLevel::INFO, string("SDK initialized for device ") + to_string(selfId));
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
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> dist(100000, 999999);
    conn.specialCode = dist(gen);
    connections_[cid] = conn;

    MessageId mid = nextMsgId_++;
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << "}";
    string payload = oss.str();
    Message handshakeMsg{mid, cid, payload, MessageFormat::HANDSHAKE, defaultPriority, true, [onSuccess, cid]() { if (onSuccess) onSuccess(cid); }};
    outgoingQueue_.push(handshakeMsg);

    log(LogLevel::INFO, string("Initiating handshake to device ") + to_string(targetId) + " connectionId=" + to_string(cid));
}

void EminentSdk::close(ConnectionId id) {
    if (connections_.count(id)) {
        if (connections_[id].onDisconnected) {
            connections_[id].onDisconnected();
        }
        connections_.erase(id);
        log(LogLevel::INFO, string("Connection ") + to_string(id) + " closed");
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
        throw runtime_error("Send failed: invalid connection ID.");
    }
    if (connections_[id].status == ConnectionStatus::PENDING) {
        throw runtime_error("Send failed: connection is still pending.");
    }

    MessageId mid = nextMsgId_++;
    Message msg{ mid, id, payload, format, priority, requireAck, onDelivered };
    outgoingQueue_.push(msg);

    log(LogLevel::DEBUG, string("Queued message id=") + to_string(mid) + " connection=" + to_string(id));
}

void EminentSdk::setDefaultPriority(ConnectionId id, Priority priority) {
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        auto match = find_if(
            connections_.begin(),
            connections_.end(),
            [id](const auto& entry) {
                return entry.second.id == id;
            }
        );
        if (match == connections_.end()) {
            log(LogLevel::WARN, string("setDefaultPriority: connection ") + to_string(id) + " not found");
            return;
        }
        it = match;
    }

    it->second.defaultPriority = priority;
    log(LogLevel::INFO, string("Connection ") + to_string(it->second.id) +
            " default priority set to " + to_string(priority));
}

void EminentSdk::setOnMessageHandler(ConnectionId id, function<void(const Message&)> handler) {
    auto it = connections_.find(id);
    if (it == connections_.end()) {
        auto match = find_if(
            connections_.begin(),
            connections_.end(),
            [id](const auto& entry) {
                return entry.second.id == id;
            }
        );
        if (match == connections_.end()) {
            log(LogLevel::WARN, string("setOnMessageHandler: connection ") + to_string(id) + " not found");
            return;
        }
        it = match;
    }

    it->second.onMessage = move(handler);
    log(LogLevel::INFO, string("Connection ") + to_string(it->second.id) + " onMessage handler updated");
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
