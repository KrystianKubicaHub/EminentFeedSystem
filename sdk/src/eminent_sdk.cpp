#include "session_manager.hpp"
#include "transport_layer.hpp"
#include "physical_layer.hpp"
#include "eminent_sdk.hpp"
#include <iostream>

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
    if (msg.format != MessageFormat::HANDSHAKE) {
        std::cout << "[EminentSdk] Message type is not HANDSHAKE. Ignoring." << std::endl;
            std::cout << "Message format: " << static_cast<int>(msg.format) << std::endl;
        return;
    }
    bool accepted = false;
    if (onIncomingConnectionDecision_) {
        accepted = onIncomingConnectionDecision_(msg.connId, msg.payload);
    }
    if (accepted) {
        std::cout << "[EminentSdk] Handshake from connId=" << msg.connId << " accepted. Adding connection to active list." << std::endl;
        Connection conn;
        conn.id = msg.connId;
        conn.remoteId = msg.connId;
        conn.defaultPriority = 0;
        conn.status = ConnectionStatus::ACTIVE;
        connections_[msg.connId] = conn;
        if (onConnectionEstablished_) {
            onConnectionEstablished_(conn.id, conn.remoteId);
        }
    } else {
        std::cout << "[EminentSdk] Handshake from connId=" << msg.connId << " rejected by user decision." << std::endl;
    }
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

    ConnectionId cid = nextConnectionId_++;
    Connection conn;
    conn.id = cid;
    conn.remoteId = targetId;
    conn.defaultPriority = defaultPriority;
    conn.onMessage = onMessage;
    conn.onTrouble = onTrouble;
    conn.onDisconnected = onDisconnected;
    conn.onConnected = onConnected;
    conn.status = ConnectionStatus::PENDING;
    connections_[cid] = conn;

    MessageId mid = nextMsgId_++;
    string payload = "ipockowanfwa";
    Message handshakeMsg{
        mid,
        cid,
        payload,
        MessageFormat::HANDSHAKE,
        defaultPriority,
        true,
        [onSuccess, cid]() {
            if (onSuccess) onSuccess(cid);
        }
    };
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
