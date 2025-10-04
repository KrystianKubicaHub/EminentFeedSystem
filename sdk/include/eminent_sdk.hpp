#pragma once

#include <common_types.hpp>
#include "session_manager.hpp"
#include "transport_layer.hpp"
#include "physical_layer.hpp"


class EminentSdk {
public:
    EminentSdk();

    void initialize(
        DeviceId selfId,
        function<void()> onSuccess,
        function<void(const string&)> onFailure,
        function<void(DeviceId)> onIncomingConnection
    );

    void connect(
        DeviceId targetId,
        Priority defaultPriority,
        function<void(ConnectionId)> onSuccess,
        function<void(const string&)> onFailure,
        function<void(const string&)> onTrouble,
        function<void()> onDisconnected,
        function<void(ConnectionId)> onConnected,
        function<void(const Message&)> onMessage
    );

    void close(ConnectionId id);

    void send(
        ConnectionId id,
        const string& payload,
        MessageFormat format,
        Priority priority,
        bool requireAck,
        function<void()> onDelivered
    );

    void handleReceivedMessage(const Message& msg);

    void getStats(
        function<void(const vector<ConnectionStats>&)> onStats,
        ConnectionId id = -1
    );

private:
    DeviceId deviceId_;
    int nextConnectionId_ = 1;
    int nextMsgId_ = 1;

    bool initialized_ = false;
    function<void(DeviceId remoteId)> onIncomingConnection_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;

    SessionManager sessionManager_;
    TransportLayer transportLayer_;
    PhysicalLayerUdp physicalLayer_;
};
