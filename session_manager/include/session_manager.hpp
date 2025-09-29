#pragma once
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>

using std::string;
using std::queue;
using std::unordered_map;
using std::vector;

using ConnectionId = int;
using MessageId = int;
using PackageId = int;
using Priority = int;

enum class MessageFormat {
    JSON,
    VIDEO
};

struct Package {
    PackageId packageId;
    MessageId messageId;
    ConnectionId connectionId;
    string payload;
    MessageFormat format;
    Priority priority;
    bool requireAck;

    enum class Status { REGISTERED, SENT, ACKED, RETRY_PENDING };
    Status status = Status::REGISTERED;

    int retryCount = 0;
    std::chrono::steady_clock::time_point lastSendTime;
};

struct SessionStats {
    int totalMessages = 0;
    int totalPackages = 0;
    int inFlight = 0;
    int retransmissions = 0;
};

class SessionManager {
public:
    explicit SessionManager(int packageSize = 100);

    void enqueueMessage(
        MessageId messageId,
        ConnectionId connectionId,
        const string& payload,
        MessageFormat format,
        Priority priority,
        bool requireAck
    );

    bool getNextPackage(Package& out);

    void ackPackage(PackageId packageId);

    void checkTimeouts(int timeoutMs);

    SessionStats getStats() const;

private:
    int packageSize_;
    PackageId nextPackageId_ = 1;

    queue<Package> outgoingQueue_;
    unordered_map<PackageId, Package> inFlight_;
};
