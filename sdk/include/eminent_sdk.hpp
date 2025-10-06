#pragma once

#include <common_types.hpp>
#include <optional>
#include "session_manager.hpp"
#include "transport_layer.hpp"

#include "physical_layer.hpp"
#include "CodingModule.hpp"


class EminentSdk {
public:
    EminentSdk(
        int localPort,
        const std::string& remoteHost,
        int remotePort
    );

    void initialize(
        DeviceId selfId,
        function<void()> onSuccess,
        function<void(const string&)> onFailure,
        function<bool(DeviceId, const std::string& payload)> onIncomingConnectionDecision,
        function<void(ConnectionId, DeviceId)> onConnectionEstablished = nullptr
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
        void onMessageReceived(const Message& msg);

    void getStats(
        function<void(const vector<ConnectionStats>&)> onStats,
        ConnectionId id = -1
    );

private:
    DeviceId deviceId_;
    int nextConnectionId_ = 2;
    int nextPrime();
    int nextMsgId_ = 1;

    bool initialized_ = false;
    std::function<bool(DeviceId, const std::string& payload)> onIncomingConnectionDecision_;
    std::function<void(ConnectionId, DeviceId)> onConnectionEstablished_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;

    SessionManager sessionManager_;
    TransportLayer transportLayer_;
    CodingModule codingModule_;
    PhysicalLayerUdp physicalLayer_;
    int localPort_;
    std::string remoteHost_;
    int remotePort_;
        struct HandshakePayload {
            bool hasDeviceId = false;
            int deviceId = 0;
            bool hasSpecialCode = false;
            int specialCode = 0;
            bool hasNewId = false;
            int newId = 0;
        };

        std::optional<HandshakePayload> parseHandshakePayload(const std::string& payload);
        void handleHandshakeRequest(const Message& msg, const HandshakePayload& payload);
        void handleHandshakeResponse(const Message& msg, const HandshakePayload& payload);
        void handleJsonMessage(const Message& msg);
        void handleVideoMessage(const Message& msg);
};
