#include "eminent_sdk.hpp"
#include <iostream>

using std::cout;
using std::endl;
using std::string;
using std::function;

void EminentSdk::initialize(
    DeviceId selfId,
    function<void()> onSuccess,
    function<void(const string&)> onFailure,
    function<void(DeviceId)> onIncomingConnection
) {
    deviceId_ = selfId;
    onIncomingConnection_ = onIncomingConnection;

    cout << "SDK initialized for device: " << selfId << endl;

    MessageId mid = nextMsgId_++;
    string handshakePayload = "HANDSHAKE_FROM:" + std::to_string(selfId);
    Message handshake{ mid, 0, handshakePayload, MessageFormat::HANDSHAKE, 10, true, nullptr };
    outgoingQueue_.push(handshake);

    cout << "Handshake message queued with ID " << mid << endl;

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
    function<void()> onDisconnected
) {
    if (targetId <= 0) {
        if (onFailure) {
            onFailure("Invalid target ID");
        }
        return;
    }

    ConnectionId cid = nextConnectionId_++;
    connections_[cid] = { cid, targetId, defaultPriority, nullptr, onTrouble, onDisconnected };

    cout << "Connecting to device " << targetId << " with connection ID " << cid << endl;
    if (onSuccess) {
        onSuccess(cid);
    }
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
    function<void(MessageId)> onQueued,
    function<void()> onDelivered
) {
    if (!connections_.count(id)) {
        cout << "Send failed: invalid connection ID." << endl;
        return;
    }

    MessageId mid = nextMsgId_++;
    Message msg{ mid, id, payload, format, priority, requireAck, onDelivered };
    outgoingQueue_.push(msg);

    cout << "Message queued with ID " << mid << " on connection " << id << endl;
    if (onQueued) {
        onQueued(mid);
    }

    if (connections_[id].onMessage) {
        connections_[id].onMessage(payload);
    }

    if (onDelivered) {
        onDelivered();
    }
}

void EminentSdk::listen(
    ConnectionId id,
    function<void(const string&)> onMessage
) {
    if (connections_.count(id)) {
        connections_[id].onMessage = onMessage;
        cout << "Listening on connection " << id << endl;
    }
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
