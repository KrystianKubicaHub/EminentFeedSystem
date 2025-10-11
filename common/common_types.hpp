#pragma once
using namespace std;

using DeviceId = int;
using ConnectionId = int;
using MessageId = int;
using PackageId = int;
using Priority = int;


#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <queue>


#pragma once

#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <queue>

using namespace std;

using DeviceId = int;
using ConnectionId = int;
using MessageId = int;
using PackageId = int;
using Priority = int;

enum class MessageFormat {
    JSON,
    VIDEO,
    HANDSHAKE,
    CONFIRMATION
};

enum class PackageStatus {
    QUEUED,
    SENT,
    ACKED,
    FAILED
};

struct Frame {
    std::vector<uint8_t> data;
};
struct Package {

    PackageId packageId;
    MessageId messageId;
    ConnectionId connId;
    int fragmentId;
    int fragmentsCount;
    std::string payload;
    MessageFormat format;
    Priority priority;
    bool requireAck;
    PackageStatus status = PackageStatus::QUEUED;
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

enum class ConnectionStatus {
    PENDING, // wysłana prośba o połączenie
    ACCEPTED, // zaakceptowaliśmy, możemy odbierać wiadomości, ale nie mamy potwierdzenia od drugiej strony
    ACTIVE, // aktywne
    FAILED // klęska
};

struct Connection {
    ConnectionId id;
    DeviceId remoteId;
    Priority defaultPriority;
    function<void(const Message&)> onMessage;
    function<void(const string&)> onTrouble;
    function<void()> onDisconnected;
    function<void(ConnectionId)> onConnected;
    ConnectionStatus status = ConnectionStatus::PENDING;
    int specialCode = 0;
};

