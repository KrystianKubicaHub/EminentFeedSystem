#include "session_manager.hpp"
#include "transport_layer.hpp"
#include "physical_layer.hpp"
#include "eminent_sdk.hpp"
#include <iostream>

using namespace std;


EminentSdk::EminentSdk()
    : sessionManager_(
        outgoingQueue_,
        [this](const Message& msg) { this->handleReceivedMessage(msg); }
        ),
        transportLayer_(sessionManager_.getOutgoingPackages(), sessionManager_),
        physicalLayer_(12345, "127.0.0.1", 12346)
{
}

void EminentSdk::handleReceivedMessage(const Message& msg) {
    auto it = connections_.find(msg.connId);
    if (it != connections_.end() && it->second.status == ConnectionStatus::ACTIVE) {
        if (it->second.onMessage) {
            it->second.onMessage(msg);
        }
    }
}

void EminentSdk::initialize(
    DeviceId selfId,
    function<void()> onSuccess,
    function<void(const string&)> onFailure,
    function<void(DeviceId)> onIncomingConnection
) {
    if (initialized_) {
        if (onFailure) {
            onFailure("SDK already initialized");
        }
        return;
    }
    deviceId_ = selfId;
    onIncomingConnection_ = onIncomingConnection;
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
