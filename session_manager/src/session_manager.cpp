#include "session_manager.hpp"
#include <iostream>
#include <chrono>

using namespace std;
using namespace std::chrono;

SessionManager::SessionManager(int packageSize) : packageSize_(packageSize) {}

void SessionManager::enqueueMessage(
    MessageId messageId,
    ConnectionId connectionId,
    const string& payload,
    MessageFormat format,
    Priority priority,
    bool requireAck
) {
    int offset = 0;
    while (offset < (int)payload.size()) {
        string fragment = payload.substr(offset, packageSize_);
        offset += packageSize_;

        Package pkg{
            nextPackageId_++,
            messageId,
            connectionId,
            fragment,
            format,
            priority,
            requireAck
        };

        outgoingQueue_.push(pkg);
    }
}

bool SessionManager::getNextPackage(Package& out) {
    if (outgoingQueue_.empty()) return false;

    out = outgoingQueue_.front();
    outgoingQueue_.pop();

    out.status = Package::Status::SENT;
    out.lastSendTime = steady_clock::now();

    inFlight_[out.packageId] = out;

    return true;
}

void SessionManager::ackPackage(PackageId packageId) {
    auto it = inFlight_.find(packageId);
    if (it != inFlight_.end()) {
        it->second.status = Package::Status::ACKED;
        inFlight_.erase(it);
    }
}

void SessionManager::checkTimeouts(int timeoutMs) {
    auto now = steady_clock::now();

    vector<PackageId> toRetry;
    for (auto& [pid, pkg] : inFlight_) {
        if (pkg.status == Package::Status::SENT) {
            auto elapsed = duration_cast<milliseconds>(now - pkg.lastSendTime).count();
            if (elapsed > timeoutMs) {
                toRetry.push_back(pid);
            }
        }
    }

    for (auto pid : toRetry) {
        auto it = inFlight_.find(pid);
        if (it != inFlight_.end()) {
            Package retryPkg = it->second;
            retryPkg.status = Package::Status::RETRY_PENDING;
            retryPkg.retryCount++;
            retryPkg.lastSendTime = steady_clock::now();

            outgoingQueue_.push(retryPkg);

            inFlight_[pid] = retryPkg;
        }
    }
}

SessionStats SessionManager::getStats() const {
    SessionStats stats;
    stats.totalPackages = nextPackageId_ - 1;
    stats.inFlight = (int)inFlight_.size();
    for (auto& [_, pkg] : inFlight_) {
        stats.retransmissions += pkg.retryCount;
    }
    return stats;
}
