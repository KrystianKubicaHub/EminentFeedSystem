#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <ValidationConfig.hpp>
#include "AbstractPhysicalLayer.hpp"
#include "SessionManager.hpp"
#include "TransportLayer.hpp"
#include "CodingModule.hpp"

using namespace std;

class EminentSdk : public LoggerBase {
public:
    EminentSdk(unique_ptr<AbstractPhysicalLayer> physicalLayer,
               const ValidationConfig& validationConfig = ValidationConfig{},
               LogLevel logLevel = LogLevel::NONE);
    EminentSdk(int localPort, const string& remoteHost, int remotePort, LogLevel logLevel = LogLevel::NONE);
    EminentSdk(int localPort, const string& remoteHost, int remotePort, const ValidationConfig& validationConfig, LogLevel logLevel = LogLevel::NONE);

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
    ConnectionId nextConnectionId_ = 2;
    MessageId nextMsgId_ = 1;
    MessageId nextMessageId();
    ConnectionId nextPrime();
    int generateSpecialCode();

    bool initialized_ = false;
    function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision_;
    function<void(ConnectionId, DeviceId)> onConnectionEstablished_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;

    ValidationConfig validationConfig_;
    SessionManager sessionManager_;
    TransportLayer transportLayer_;
    CodingModule codingModule_;
    unique_ptr<AbstractPhysicalLayer> physicalLayer_;
    int localPort_ = 0;
    string remoteHost_;
    int remotePort_ = 0;

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
