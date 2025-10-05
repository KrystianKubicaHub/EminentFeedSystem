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
    std::queue<Message>& sdkQueue_;
    EminentSdk& sdk_;
    size_t maxPacketSize_;
    PackageId nextPackageId_ = 1;
    std::queue<Package> outgoingPackages_;
    std::unordered_map<MessageId, bool> messageStatus_; // true = SENT
    std::unordered_map<MessageId, std::vector<Package>> receivedPackages_;
    std::thread worker_;
    std::mutex queueMutex_;
    bool stopWorker_ = false;
public:
    bool getNextPackage(Package& out);
    void receivePackage(const Package& pkg);
    std::queue<Package>& getOutgoingPackages() { return outgoingPackages_; }
    ~SessionManager();
};
