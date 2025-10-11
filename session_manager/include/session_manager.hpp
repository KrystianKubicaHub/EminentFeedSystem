#pragma once
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>
#include <common_types.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

using namespace std;

// aliasy typów w common_types.hpp




class EminentSdk; // forward

class SessionManager {
public:
    SessionManager(std::queue<Message>& sdkQueue, EminentSdk& sdk, size_t maxPacketSize = 256);

    void processMessages();

private:
    struct PendingPackageInfo {
        Package pkg;
        std::chrono::steady_clock::time_point lastSent;
        int attempts = 0;
    };

    struct PendingMessageInfo {
        Message message;
        std::unordered_map<PackageId, PendingPackageInfo> packages;
    };

    std::queue<Message>& sdkQueue_;
    EminentSdk& sdk_;
    size_t maxPacketSize_;
    PackageId nextPackageId_ = 1;
    MessageId nextAckMessageId_ = -1;
    std::queue<Package> outgoingPackages_;
    std::unordered_map<MessageId, PendingMessageInfo> pendingMessages_;
    std::unordered_map<MessageId, std::vector<Package>> receivedPackages_;
    std::unordered_map<PackageId, MessageId> packageToMessage_;
    std::chrono::milliseconds retransmitInterval_{500};
    std::chrono::milliseconds workerSleepInterval_{20};
    int maxRetransmitAttempts_ = 5;
    std::thread worker_;
    std::mutex queueMutex_;
    bool stopWorker_ = false;
    void workerLoop();
    void processSdkQueueLocked(const std::chrono::steady_clock::time_point& now);
    void retransmitPendingLocked(const std::chrono::steady_clock::time_point& now);
    void sendPackageLocked(PendingPackageInfo& info, const std::chrono::steady_clock::time_point& now);
    void sendAckForPackageLocked(const Package& pkg);
    void handleAckPackage(const Package& pkg);
    std::optional<PackageId> parseAckPayload(const std::string& payload) const;
public:
    bool getNextPackage(Package& out);
    void receivePackage(const Package& pkg);
    std::queue<Package>& getOutgoingPackages() { return outgoingPackages_; }
    ~SessionManager();
};
