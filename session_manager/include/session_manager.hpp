#pragma once
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>
#include <common_types.hpp>
#include <logging.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;

class EminentSdk;

class SessionManager : public LoggerBase {
public:
    SessionManager(queue<Message>& sdkQueue, EminentSdk& sdk, size_t maxPacketSize = 256);

    void processMessages();

private:
    struct PendingPackageInfo {
        Package pkg;
        chrono::steady_clock::time_point lastSent;
        int attempts = 0;
    };

    struct PendingMessageInfo {
        Message message;
        unordered_map<PackageId, PendingPackageInfo> packages;
    };

    queue<Message>& sdkQueue_;
    EminentSdk& sdk_;
    size_t maxPacketSize_;
    PackageId nextPackageId_ = 1;
    MessageId nextAckMessageId_ = -1;
    queue<Package> outgoingPackages_;
    unordered_map<MessageId, PendingMessageInfo> pendingMessages_;
    unordered_map<MessageId, vector<Package>> receivedPackages_;
    unordered_map<PackageId, MessageId> packageToMessage_;
    chrono::milliseconds retransmitInterval_{500};
    chrono::milliseconds workerSleepInterval_{20};
    int maxRetransmitAttempts_ = 5;
    thread worker_;
    mutex queueMutex_;
    bool stopWorker_ = false;
    void workerLoop();
    void processSdkQueueLocked(const chrono::steady_clock::time_point& now, vector<function<void()>>& callbacks);
    void retransmitPendingLocked(const chrono::steady_clock::time_point& now);
    void sendPackageLocked(PendingPackageInfo& info, const chrono::steady_clock::time_point& now);
    void sendAckForPackageLocked(const Package& pkg);
    void handleAckPackage(const Package& pkg);
    optional<PackageId> parseAckPayload(const string& payload) const;
public:
    bool getNextPackage(Package& out);
    void receivePackage(const Package& pkg);
    queue<Package>& getOutgoingPackages() { return outgoingPackages_; }
    ~SessionManager();
};
