#pragma once

#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <common_types.hpp>
#include <logging.hpp>
#include "session_manager.hpp"
#include "transport_layer.hpp"
#include "physical_layer.hpp"
#include "CodingModule.hpp"

using namespace std;

class EminentSdk : public LoggerBase {
public:
    EminentSdk(int localPort, const string& remoteHost, int remotePort);

    void initialize(
        DeviceId selfId,
        function<void()> onSuccess,
        function<void(const string&)> onFailure,
        function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision,
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
    void complexConsoleInfo(const string& title = "");
    void setDefaultPriority(ConnectionId id, Priority priority);
    void setOnMessageHandler(ConnectionId id, function<void(const Message&)> handler);

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
    function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision_;
    function<void(ConnectionId, DeviceId)> onConnectionEstablished_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;

    SessionManager sessionManager_;
    TransportLayer transportLayer_;
    CodingModule codingModule_;
    PhysicalLayerUdp physicalLayer_;
    int localPort_;
    string remoteHost_;
    int remotePort_;

    struct HandshakePayload {
        bool hasDeviceId = false;
        int deviceId = 0;
        bool hasSpecialCode = false;
        int specialCode = 0;
        bool hasNewId = false;
        int newId = 0;
        bool hasFinalConfirmation = false;
        bool finalConfirmation = false;
    };

    optional<HandshakePayload> parseHandshakePayload(const string& payload);
    void handleHandshakeRequest(const Message& msg, const HandshakePayload& payload);
    void handleHandshakeResponse(const Message& msg, const HandshakePayload& payload);
    void handleHandshakeFinalConfirmation(const Message& msg, const HandshakePayload& payload);
    void handleJsonMessage(const Message& msg);
    void handleVideoMessage(const Message& msg);
    string statusToString(ConnectionStatus status) const;
};
