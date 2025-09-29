#pragma once
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <queue>

using std::string;
using std::function;
using std::vector;
using std::unordered_map;
using std::queue;

using DeviceId = int;
using ConnectionId = int;
using MessageId = int;
using Priority = int;

enum class MessageFormat {
    JSON,
    VIDEO,
    HANDSHAKE
};

struct ConnectionStats {
    ConnectionId id;
    double avgLatencyMs;
    double packetLossPercent;
    double throughputMbps;
    int queuedMessages;
};

struct Message {
    MessageId id;
    ConnectionId connId;
    string payload;
    MessageFormat format;
    Priority priority;
    bool requireAck;
    function<void()> onDelivered;
};

struct Connection {
    ConnectionId id;
    DeviceId remoteId;
    Priority defaultPriority;
    function<void(const string&)> onMessage;
    function<void(const string&)> onTrouble;
    function<void()> onDisconnected;
};

class EminentSdk {
public:
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
        function<void()> onDisconnected
    );

    void close(ConnectionId id);

    void send(
        ConnectionId id,
        const string& payload,
        MessageFormat format,
        Priority priority,
        bool requireAck,
        function<void(MessageId)> onQueued,
        function<void()> onDelivered
    );

    void listen(
        ConnectionId id,
        function<void(const string&)> onMessage
    );

    void getStats(
        function<void(const vector<ConnectionStats>&)> onStats,
        ConnectionId id = -1
    );

private:
    DeviceId deviceId_;
    int nextConnectionId_ = 1;
    int nextMsgId_ = 1;

    function<void(DeviceId remoteId)> onIncomingConnection_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;
};
