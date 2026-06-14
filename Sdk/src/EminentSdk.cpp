#include "EminentSdk.hpp"

#include "AbstractPhysicalLayer.hpp"
#include "PhysicalLayerUdp.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std;
using namespace chrono;

EminentSdk::EminentSdk(unique_ptr<AbstractPhysicalLayer> physicalLayer,
                       const ValidationConfig& validationConfig,
                       LogLevel logLevel)
    : LoggerBase("EminentSdk"),
      validationConfig_(validationConfig),
            sessionManager_(outgoingQueue_, *this, validationConfig_, validationConfig_.maxPayloadLengthBytes()),
      transportLayer_(sessionManager_.getOutgoingPackages(), sessionManager_, validationConfig_),
      codingModule_(transportLayer_.getOutgoingFrames(), transportLayer_, validationConfig_),
      physicalLayer_(std::move(physicalLayer)),
      localPort_(0),
      remoteHost_(),
      remotePort_(0) {
    if (!physicalLayer_) {
        throw invalid_argument("physicalLayer must not be null");
    }
    if (auto* udpLayer = dynamic_cast<PhysicalLayerUdp*>(physicalLayer_.get())) {
        localPort_ = udpLayer->localPort();
        remoteHost_ = udpLayer->remoteHost();
        remotePort_ = udpLayer->remotePort();
    }
    LoggerConfig::setLevel(logLevel);
    physicalLayer_->configure(codingModule_.getOutgoingFrames(), codingModule_, validationConfig_);
    physicalLayer_->start();
}

EminentSdk::EminentSdk(int localPort, const string& remoteHost, int remotePort, LogLevel logLevel)
    : EminentSdk(localPort, remoteHost, remotePort, ValidationConfig{}, logLevel) {}

EminentSdk::EminentSdk(int localPort, const string& remoteHost, int remotePort, const ValidationConfig& validationConfig, LogLevel logLevel)
    : EminentSdk(make_unique<PhysicalLayerUdp>(localPort, remoteHost, remotePort),
                 validationConfig,
                 logLevel) {
    localPort_ = localPort;
    remoteHost_ = remoteHost;
    remotePort_ = remotePort;
}

EminentSdk::~EminentSdk() {
    shutdown();
}


void EminentSdk::onMessageReceived(const Message& msg) {
    lock_guard<recursive_mutex> lock(mutex_);
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
        case MessageFormat::DISCONNECT:
            handleDisconnectMessage(msg);
            break;
        case MessageFormat::HEARTBEAT:
            handleHeartbeat(msg);
            break;
        case MessageFormat::HEARTBEAT_ACK:
            handleHeartbeatAck(msg);
            break;
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
    // VIDEO format is used internally for binary user data
    auto it = findConnection(msg.connId);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("Binary message for unknown connectionId=") + to_string(msg.connId));
        return;
    }

    log(LogLevel::DEBUG, string("Binary message on connection ") + to_string(it->second.id) +
        " size=" + to_string(msg.payload.size()));

    if (it->second.onMessage) {
        it->second.onMessage(msg);
    } else {
        log(LogLevel::WARN, string("No onMessage callback for connection ") + to_string(it->second.id));
    }
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
    lock_guard<recursive_mutex> lock(mutex_);
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
    try {
        validationConfig_.validateConnectionId(msg.connId);
        validationConfig_.validateDeviceId(payload.deviceId);
        validationConfig_.validateSpecialCode(payload.specialCode);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Handshake request rejected: ") + ex.what());
        return;
    }

    bool accepted = false;
    if (onIncomingConnectionDecision_) {
        accepted = onIncomingConnectionDecision_(payload.deviceId, msg.payload);
    }
    if (!accepted) {
        log(LogLevel::INFO, string("Handshake connId=") + to_string(msg.connId) + " rejected by decision");
        return;
    }

    log(LogLevel::INFO, string("Handshake connId=") + to_string(msg.connId) + " accepted -> sending response");

    ConnectionId myConnId = nextPrime();
    long long combinedProduct = static_cast<long long>(msg.connId) * static_cast<long long>(myConnId);
    if (combinedProduct <= 0 || combinedProduct > numeric_limits<int>::max()) {
        log(LogLevel::WARN, "Handshake combined connection id overflow");
        return;
    }
    int combinedId = static_cast<int>(combinedProduct);
    try {
        validationConfig_.validateConnectionId(combinedId);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Handshake combined connection id invalid: ") + ex.what());
        return;
    }

    Connection conn;
    conn.id = combinedId;
    conn.remoteId = payload.deviceId;
    conn.defaultPriority = 0;
    conn.status = ConnectionStatus::ACCEPTED;
    conn.specialCode = payload.specialCode;
    connections_[combinedId] = conn;

    log(LogLevel::INFO, string("Connection ") + to_string(combinedId) + " status set to ACCEPTED");

    MessageId mid = nextMessageId();
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"newId\": " << myConnId << "}";
    string respPayload = oss.str();
    Message respMsg{mid, msg.connId, respPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    try {
        validationConfig_.validateMessage(respMsg);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to queue handshake response: ") + ex.what());
        connections_.erase(combinedId);
        return;
    }
    outgoingQueue_.push(respMsg);
}

void EminentSdk::handleHandshakeResponse(const Message& msg, const HandshakePayload& payload) {
    try {
        validationConfig_.validateConnectionId(msg.connId);
        validationConfig_.validateDeviceId(payload.deviceId);
        validationConfig_.validateSpecialCode(payload.specialCode);
        if (payload.hasNewId) {
            validationConfig_.validateConnectionId(payload.newId);
        }
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Handshake response invalid: ") + ex.what());
        return;
    }

    auto it = connections_.find(msg.connId);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("Handshake response for unknown connectionId=") + to_string(msg.connId));
        return;
    }

    Connection conn = it->second;
    connections_.erase(it);

    long long combinedProduct = static_cast<long long>(msg.connId) * static_cast<long long>(payload.newId);
    if (combinedProduct <= 0 || combinedProduct > numeric_limits<int>::max()) {
        log(LogLevel::WARN, "Handshake response combined connection id overflow");
        return;
    }
    int combinedId = static_cast<int>(combinedProduct);
    try {
        validationConfig_.validateConnectionId(combinedId);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Handshake response combined connection id invalid: ") + ex.what());
        return;
    }
    conn.id = combinedId;
    conn.remoteId = payload.deviceId;
    conn.specialCode = payload.specialCode;
    conn.status = ConnectionStatus::ACTIVE;
    connections_[combinedId] = conn;

    // Remove from pending handshakes (handshake succeeded)
    pendingHandshakes_.erase(
        remove_if(pendingHandshakes_.begin(), pendingHandshakes_.end(),
            [&](const PendingHandshake& ph) { return ph.initialCid == msg.connId; }),
        pendingHandshakes_.end()
    );

    // Migrate heartbeat state from initial cid to final combined id
    auto hbIt = heartbeats_.find(msg.connId);
    if (hbIt != heartbeats_.end()) {
        HeartbeatState hb = hbIt->second;
        hb.lastSent = steady_clock::now();
        hb.lastReceived = steady_clock::now();
        heartbeats_.erase(hbIt);
        heartbeats_[combinedId] = hb;
    }

    log(LogLevel::INFO, string("Connection ") + to_string(combinedId) + " is now ACTIVE");

    if (conn.onConnected) {
        conn.onConnected(conn.id);
    }

    MessageId ackId = nextMessageId();
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << ", \"finalConfirmation\": true}";
    string ackPayload = oss.str();
    Message finalAck{ackId, combinedId, ackPayload, MessageFormat::HANDSHAKE, 0, false, nullptr};
    try {
        validationConfig_.validateMessage(finalAck);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to queue final handshake ack: ") + ex.what());
        connections_.erase(combinedId);
        return;
    }
    outgoingQueue_.push(finalAck);
}

void EminentSdk::handleHandshakeFinalConfirmation(const Message& msg, const HandshakePayload& payload) {
    try {
        validationConfig_.validateConnectionId(msg.connId);
        if (payload.hasDeviceId) {
            validationConfig_.validateDeviceId(payload.deviceId);
        }
        if (payload.hasSpecialCode) {
            validationConfig_.validateSpecialCode(payload.specialCode);
        }
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Final confirmation invalid: ") + ex.what());
        return;
    }

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

    try {
        validationConfig_.validateDeviceId(selfId);
    } catch (const exception& ex) {
        log(LogLevel::ERROR, string("initialize: invalid device id: ") + ex.what());
        if (onFailure) {
            onFailure(ex.what());
        }
        return;
    }
    deviceId_ = selfId;

    onIncomingConnectionDecision_ = onIncomingConnectionDecision;
    onConnectionEstablished_ = onConnectionEstablished; 
   
    initialized_ = true;

    // Start heartbeat worker thread
    stopHeartbeat_ = false;
    heartbeatWorker_ = thread([this]() { heartbeatLoop(); });

    log(LogLevel::INFO, string("SDK initialized for device ") + to_string(selfId));
    if (onSuccess) {
        onSuccess();
    }
}

MessageId EminentSdk::nextMessageId() {
    try {
        validationConfig_.validateMessageId(nextMsgId_);
    } catch (const exception& ex) {
        throw runtime_error(string("Unable to allocate message id: ") + ex.what());
    }
    return nextMsgId_++;
}

int EminentSdk::generateSpecialCode() {
    uint8_t bits = validationConfig_.specialCodeBitWidth();
    uint64_t maxValue = (bits == 32) ? numeric_limits<uint32_t>::max() : ((1ULL << bits) - 1ULL);
    maxValue = min<uint64_t>(maxValue, numeric_limits<int>::max());

    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<uint64_t> dist(0, maxValue);

    while (true) {
        int candidate = static_cast<int>(dist(gen));
        try {
            validationConfig_.validateSpecialCode(candidate);
            return candidate;
        } catch (const exception&) {
            // regenerate
        }
    }
}

ConnectionId EminentSdk::nextPrime() {
    ConnectionId candidate = nextConnectionId_;
    while (true) {
        try {
            validationConfig_.validateConnectionId(candidate);
        } catch (const exception& ex) {
            throw runtime_error(string("Unable to allocate connection id: ") + ex.what());
        }

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
        if (candidate == numeric_limits<ConnectionId>::max()) {
            throw runtime_error("Unable to allocate connection id: exhausted range");
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
    function<void(const Message&)> onMessage,
    chrono::milliseconds heartbeatInterval,
    function<void(ConnectionId)> onHeartbeatMissed,
    chrono::milliseconds handshakeTimeout
) {
    lock_guard<recursive_mutex> lock(mutex_);
    try {
        validationConfig_.validateDeviceId(targetId);
        validationConfig_.validatePriority(defaultPriority);
    } catch (const exception& ex) {
        if (onFailure) {
            onFailure(ex.what());
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
    conn.specialCode = generateSpecialCode();
    connections_[cid] = conn;

    MessageId mid = nextMessageId();
    ostringstream oss;
    oss << "{\"deviceId\": " << deviceId_ << ", \"specialCode\": " << conn.specialCode << "}";
    string payload = oss.str();
    Message handshakeMsg{mid, cid, payload, MessageFormat::HANDSHAKE, defaultPriority, true, [onSuccess, cid]() { if (onSuccess) onSuccess(cid); }};
    try {
        validationConfig_.validateMessage(handshakeMsg);
    } catch (const exception& ex) {
        connections_.erase(cid);
        if (onFailure) {
            onFailure(ex.what());
        }
        return;
    }
    outgoingQueue_.push(handshakeMsg);

    // Store heartbeat config under initial cid — will be migrated to combined id after handshake
    HeartbeatState hb;
    hb.interval = heartbeatInterval;
    hb.lastReceived = steady_clock::now();
    hb.lastSent = steady_clock::now();
    hb.waitingForResponse = false;
    hb.onMissed = onHeartbeatMissed;
    heartbeats_[cid] = hb;

    // Register handshake timeout
    PendingHandshake ph;
    ph.initialCid = cid;
    ph.deadline = steady_clock::now() + handshakeTimeout;
    ph.onFailure = onFailure;
    pendingHandshakes_.push_back(ph);

    log(LogLevel::INFO, string("Initiating handshake to device ") + to_string(targetId) +
        " connectionId=" + to_string(cid) +
        " timeout=" + to_string(handshakeTimeout.count()) + "ms");
}

void EminentSdk::disconnect(ConnectionId id) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("disconnect: connection ") + to_string(id) + " not found");
        return;
    }

    // Send disconnect message to remote side
    sendDisconnectMessage(it->second.id);

    // Invoke local onDisconnected callback
    if (it->second.onDisconnected) {
        it->second.onDisconnected();
    }

    // Remove heartbeat state
    heartbeats_.erase(it->second.id);

    // Remove connection
    ConnectionId actualId = it->second.id;
    connections_.erase(it);
    log(LogLevel::INFO, string("Connection ") + to_string(actualId) + " disconnected");
}

void EminentSdk::close(ConnectionId id) {
    disconnect(id);
}

void EminentSdk::send(
    ConnectionId id,
    const string& payload,
    MessageFormat format,
    Priority priority,
    bool requireAck,
    function<void()> onDelivered
) {
    lock_guard<recursive_mutex> lock(mutex_);
    if (!connections_.count(id)) {
        throw runtime_error("Send failed: invalid connection ID.");
    }
    if (connections_[id].status == ConnectionStatus::PENDING) {
        throw runtime_error("Send failed: connection is still pending.");
    }

    try {
        validationConfig_.validatePriority(priority);
    } catch (const exception& ex) {
        throw runtime_error(string("Send failed: ") + ex.what());
    }

    MessageId mid = nextMessageId();
    Message msg{ mid, id, payload, format, priority, requireAck, onDelivered };
    try {
        validationConfig_.validateMessage(msg);
    } catch (const exception& ex) {
        throw runtime_error(string("Send failed: ") + ex.what());
    }
    outgoingQueue_.push(msg);

    log(LogLevel::DEBUG, string("Queued message id=") + to_string(mid) + " connection=" + to_string(id));
}

void EminentSdk::setDefaultPriority(ConnectionId id, Priority priority) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("setDefaultPriority: connection ") + to_string(id) + " not found");
        return;
    }

    try {
        validationConfig_.validatePriority(priority);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("setDefaultPriority failed: ") + ex.what());
        return;
    }

    it->second.defaultPriority = priority;
    log(LogLevel::INFO, string("Connection ") + to_string(it->second.id) +
            " default priority set to " + to_string(priority));
}

void EminentSdk::setOnMessageHandler(ConnectionId id, function<void(const Message&)> handler) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("setOnMessageHandler: connection ") + to_string(id) + " not found");
        return;
    }

    it->second.onMessage = std::move(handler);
    log(LogLevel::INFO, string("Connection ") + to_string(it->second.id) + " onMessage handler updated");
}

void EminentSdk::getStats(
    function<void(const vector<ConnectionStats>&)> onStats,
    ConnectionId id
) {
    lock_guard<recursive_mutex> lock(mutex_);
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

// ============================================================
// Simplified send (uses connection default priority)
// ============================================================

void EminentSdk::send(
    ConnectionId id,
    const string& payload,
    function<void()> onDelivered
) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        throw runtime_error("Send failed: invalid connection ID.");
    }
    send(id, payload, MessageFormat::JSON, it->second.defaultPriority, true, onDelivered);
}

// ============================================================
// Binary send
// ============================================================

void EminentSdk::sendBinary(
    ConnectionId id,
    const vector<uint8_t>& data,
    function<void()> onDelivered
) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        throw runtime_error("sendBinary failed: invalid connection ID.");
    }
    sendBinary(id, data, it->second.defaultPriority, true, onDelivered);
}

void EminentSdk::sendBinary(
    ConnectionId id,
    const vector<uint8_t>& data,
    Priority priority,
    bool requireAck,
    function<void()> onDelivered
) {
    lock_guard<recursive_mutex> lock(mutex_);
    if (!connections_.count(id)) {
        auto it = findConnection(id);
        if (it == connections_.end()) {
            throw runtime_error("sendBinary failed: invalid connection ID.");
        }
        id = it->second.id;
    }
    if (connections_.count(id) && connections_[id].status == ConnectionStatus::PENDING) {
        throw runtime_error("sendBinary failed: connection is still pending.");
    }

    try {
        validationConfig_.validatePriority(priority);
    } catch (const exception& ex) {
        throw runtime_error(string("sendBinary failed: ") + ex.what());
    }

    // Store binary data in string (std::string can hold arbitrary bytes)
    string payload(data.begin(), data.end());

    MessageId mid = nextMessageId();
    Message msg{ mid, id, payload, MessageFormat::VIDEO, priority, requireAck, onDelivered };
    try {
        validationConfig_.validateMessage(msg);
    } catch (const exception& ex) {
        throw runtime_error(string("sendBinary failed: ") + ex.what());
    }
    outgoingQueue_.push(msg);

    log(LogLevel::DEBUG, string("Queued binary message id=") + to_string(mid) +
        " connection=" + to_string(id) + " size=" + to_string(data.size()));
}

// ============================================================
// setOnDisconnected
// ============================================================

void EminentSdk::setOnDisconnected(ConnectionId id, function<void()> handler) {
    lock_guard<recursive_mutex> lock(mutex_);
    auto it = findConnection(id);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("setOnDisconnected: connection ") + to_string(id) + " not found");
        return;
    }
    it->second.onDisconnected = std::move(handler);
    log(LogLevel::INFO, string("Connection ") + to_string(it->second.id) + " onDisconnected handler updated");
}

// ============================================================
// getActiveConnectionIds / getConnectedDeviceIds
// ============================================================

vector<ConnectionId> EminentSdk::getActiveConnectionIds() const {
    lock_guard<recursive_mutex> lock(mutex_);
    vector<ConnectionId> ids;
    for (const auto& [cid, conn] : connections_) {
        if (conn.status == ConnectionStatus::ACTIVE) {
            ids.push_back(conn.id);
        }
    }
    return ids;
}

vector<DeviceId> EminentSdk::getConnectedDeviceIds() const {
    lock_guard<recursive_mutex> lock(mutex_);
    vector<DeviceId> ids;
    for (const auto& [cid, conn] : connections_) {
        if (conn.status == ConnectionStatus::ACTIVE) {
            ids.push_back(conn.remoteId);
        }
    }
    return ids;
}

// ============================================================
// Disconnect protocol
// ============================================================

void EminentSdk::sendDisconnectMessage(ConnectionId id) {
    try {
        MessageId mid = nextMessageId();
        ostringstream oss;
        oss << "{\"deviceId\": " << deviceId_ << ", \"connId\": " << id << "}";
        string payload = oss.str();
        Message msg{mid, id, payload, MessageFormat::DISCONNECT, 0, false, nullptr};
        validationConfig_.validateMessage(msg);
        outgoingQueue_.push(msg);
        log(LogLevel::INFO, string("Sent DISCONNECT for connection ") + to_string(id));
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to send DISCONNECT: ") + ex.what());
    }
}

void EminentSdk::handleDisconnectMessage(const Message& msg) {
    auto it = findConnection(msg.connId);
    if (it == connections_.end()) {
        log(LogLevel::WARN, string("DISCONNECT for unknown connection ") + to_string(msg.connId));
        return;
    }

    log(LogLevel::INFO, string("Received DISCONNECT for connection ") + to_string(it->second.id) +
        " from device " + to_string(it->second.remoteId));

    // Invoke onDisconnected callback
    if (it->second.onDisconnected) {
        it->second.onDisconnected();
    }

    // Remove heartbeat state
    heartbeats_.erase(it->second.id);

    // Remove connection
    connections_.erase(it);
}

// ============================================================
// Heartbeat
// ============================================================

void EminentSdk::heartbeatLoop() {
    while (!stopHeartbeat_.load()) {
        {
            lock_guard<recursive_mutex> lock(mutex_);
            auto now = steady_clock::now();

            // Check for handshake timeouts
            checkHandshakeTimeouts();

            for (auto& [connId, hb] : heartbeats_) {
                // Only send heartbeats for ACTIVE connections
                auto it = connections_.find(connId);
                if (it == connections_.end() || it->second.status != ConnectionStatus::ACTIVE) {
                    continue;
                }

                // Check if we're waiting for a response that's overdue
                if (hb.waitingForResponse) {
                    auto elapsed = duration_cast<milliseconds>(now - hb.lastSent);
                    if (elapsed >= hb.interval) {
                        // Heartbeat missed!
                        log(LogLevel::WARN, string("Heartbeat missed for connection ") + to_string(connId));
                        if (hb.onMissed) {
                            hb.onMissed(connId);
                        }
                        hb.waitingForResponse = false;
                        // Immediately send next heartbeat
                        sendHeartbeat(connId);
                        hb.lastSent = now;
                        hb.waitingForResponse = true;
                    }
                } else {
                    // Time to send a new heartbeat?
                    auto elapsed = duration_cast<milliseconds>(now - hb.lastSent);
                    if (elapsed >= hb.interval) {
                        sendHeartbeat(connId);
                        hb.lastSent = now;
                        hb.waitingForResponse = true;
                    }
                }
            }
        } // mutex released before sleep

        this_thread::sleep_for(100ms);
    }
}

void EminentSdk::sendHeartbeat(ConnectionId connId) {
    try {
        MessageId mid = nextMessageId();
        ostringstream oss;
        oss << "{\"deviceId\": " << deviceId_ << ", \"ts\": " 
            << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << "}";
        string payload = oss.str();
        Message msg{mid, connId, payload, MessageFormat::HEARTBEAT, 0, false, nullptr};
        validationConfig_.validateMessage(msg);
        outgoingQueue_.push(msg);
        log(LogLevel::DEBUG, string("Sent HEARTBEAT on connection ") + to_string(connId));
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to send HEARTBEAT: ") + ex.what());
    }
}

void EminentSdk::handleHeartbeat(const Message& msg) {
    // Respond with HEARTBEAT_ACK
    log(LogLevel::DEBUG, string("Received HEARTBEAT on connection ") + to_string(msg.connId));
    try {
        MessageId mid = nextMessageId();
        ostringstream oss;
        oss << "{\"deviceId\": " << deviceId_ << ", \"ts\": "
            << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << "}";
        string payload = oss.str();
        Message ack{mid, msg.connId, payload, MessageFormat::HEARTBEAT_ACK, 0, false, nullptr};
        validationConfig_.validateMessage(ack);
        outgoingQueue_.push(ack);
        log(LogLevel::DEBUG, string("Sent HEARTBEAT_ACK on connection ") + to_string(msg.connId));
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to send HEARTBEAT_ACK: ") + ex.what());
    }
}

void EminentSdk::handleHeartbeatAck(const Message& msg) {
    log(LogLevel::DEBUG, string("Received HEARTBEAT_ACK on connection ") + to_string(msg.connId));
    auto it = heartbeats_.find(msg.connId);
    if (it != heartbeats_.end()) {
        it->second.lastReceived = steady_clock::now();
        it->second.waitingForResponse = false;
    }
}

// ============================================================
// Shutdown
// ============================================================

void EminentSdk::shutdown() {
    if (shutdownRequested_.exchange(true)) {
        return; // Already shutting down
    }

    log(LogLevel::INFO, "Shutdown requested");

    // Stop heartbeat worker (must NOT hold mutex during join — worker locks mutex)
    stopHeartbeat_ = true;
    if (heartbeatWorker_.joinable()) {
        heartbeatWorker_.join();
    }

    // Now safe to lock — heartbeat thread is stopped
    lock_guard<recursive_mutex> lock(mutex_);

    // Disconnect all active connections (send DISCONNECT to each)
    vector<ConnectionId> toDisconnect;
    for (const auto& [cid, conn] : connections_) {
        if (conn.status == ConnectionStatus::ACTIVE) {
            toDisconnect.push_back(cid);
        }
    }
    for (ConnectionId cid : toDisconnect) {
        sendDisconnectMessage(cid);
        if (connections_.count(cid) && connections_[cid].onDisconnected) {
            connections_[cid].onDisconnected();
        }
    }
    connections_.clear();
    heartbeats_.clear();
    pendingHandshakes_.clear();

    // Allow time for disconnect messages to be sent
    this_thread::sleep_for(50ms);

    log(LogLevel::INFO, "Shutdown complete");
}

// ============================================================
// Handshake timeout check
// ============================================================

void EminentSdk::checkHandshakeTimeouts() {
    auto now = steady_clock::now();
    vector<function<void()>> callbacks;

    auto it = pendingHandshakes_.begin();
    while (it != pendingHandshakes_.end()) {
        // Check if this connection is still PENDING (not yet completed handshake)
        auto connIt = connections_.find(it->initialCid);
        if (connIt == connections_.end()) {
            // Connection was already removed (migrated to combined id = handshake succeeded)
            it = pendingHandshakes_.erase(it);
            continue;
        }

        if (connIt->second.status == ConnectionStatus::ACTIVE) {
            // Already active — handshake completed before timeout
            it = pendingHandshakes_.erase(it);
            continue;
        }

        if (now >= it->deadline) {
            // Timeout! Remove the pending connection and fire onFailure
            log(LogLevel::WARN, string("Handshake timeout for connectionId=") + to_string(it->initialCid));

            auto failCb = it->onFailure;
            ConnectionId cid = it->initialCid;

            // Cleanup heartbeat state
            heartbeats_.erase(cid);
            // Remove the pending connection
            connections_.erase(connIt);

            it = pendingHandshakes_.erase(it);

            if (failCb) {
                callbacks.push_back([failCb]() {
                    failCb("Handshake timeout: remote device did not respond");
                });
            }
        } else {
            ++it;
        }
    }

    // Fire callbacks outside the loop to avoid issues
    for (auto& cb : callbacks) {
        cb();
    }
}

// ============================================================
// Retransmission configuration
// ============================================================

void EminentSdk::setRetransmissionConfig(int maxAttempts, chrono::milliseconds interval) {
    if (maxAttempts < 1) {
        log(LogLevel::WARN, "setRetransmissionConfig: maxAttempts must be >= 1, ignoring");
        return;
    }
    if (interval.count() < 10) {
        log(LogLevel::WARN, "setRetransmissionConfig: interval must be >= 10ms, ignoring");
        return;
    }

    sessionManager_.setRetransmissionConfig(maxAttempts, interval);
    log(LogLevel::INFO, string("Retransmission config updated: maxAttempts=") +
        to_string(maxAttempts) + " interval=" + to_string(interval.count()) + "ms");
}

int EminentSdk::getMaxRetransmitAttempts() const {
    return sessionManager_.getMaxRetransmitAttempts();
}

chrono::milliseconds EminentSdk::getRetransmitInterval() const {
    return sessionManager_.getRetransmitInterval();
}

// ============================================================
// Transport error callback
// ============================================================

void EminentSdk::setOnTransportError(function<void(const string&)> handler) {
    onTransportError_ = std::move(handler);
    log(LogLevel::INFO, "Transport error handler registered");
}

// ============================================================
// findConnection helper
// ============================================================

unordered_map<int, Connection>::iterator EminentSdk::findConnection(ConnectionId id) {
    auto it = connections_.find(id);
    if (it != connections_.end()) {
        return it;
    }
    return find_if(
        connections_.begin(),
        connections_.end(),
        [id](const auto& entry) {
            return entry.second.id == id;
        }
    );
}
