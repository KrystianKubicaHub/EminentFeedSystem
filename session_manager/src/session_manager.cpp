#include "session_manager.hpp"
#include "eminent_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std::chrono;

SessionManager::SessionManager(std::queue<Message>& sdkQueue, EminentSdk& sdk, size_t maxPacketSize)
    : sdkQueue_(sdkQueue), sdk_(sdk), maxPacketSize_(maxPacketSize) {
    worker_ = std::thread([this]() { workerLoop(); });
}

SessionManager::~SessionManager() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopWorker_ = true;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SessionManager::workerLoop() {
    while (true) {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stopWorker_) {
                break;
            }
            processSdkQueueLocked(now);
            retransmitPendingLocked(now);
        }
        std::this_thread::sleep_for(workerSleepInterval_);
    }
}

void SessionManager::processMessages() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(queueMutex_);
    processSdkQueueLocked(now);
    retransmitPendingLocked(now);
}

void SessionManager::processSdkQueueLocked(const std::chrono::steady_clock::time_point& now) {
    while (!sdkQueue_.empty()) {
        Message msg = sdkQueue_.front();
        sdkQueue_.pop();

        int total = static_cast<int>((msg.payload.size() + maxPacketSize_ - 1) / maxPacketSize_);
        if (total <= 0) {
            total = 1;
        }

        PendingMessageInfo pending;
        pending.message = msg;

        for (int frag = 0; frag < total; ++frag) {
            std::string fragment = msg.payload.substr(static_cast<size_t>(frag) * maxPacketSize_, maxPacketSize_);
            Package pkg{
                nextPackageId_++,
                msg.id,
                msg.connId,
                frag,
                total,
                fragment,
                msg.format,
                msg.priority,
                msg.requireAck,
                PackageStatus::QUEUED
            };

            PendingPackageInfo info;
            info.pkg = pkg;
            sendPackageLocked(info, now);

            pending.packages.emplace(pkg.packageId, info);
            packageToMessage_[pkg.packageId] = msg.id;
        }

        pendingMessages_[msg.id] = std::move(pending);
    }
}

void SessionManager::retransmitPendingLocked(const std::chrono::steady_clock::time_point& now) {
    for (auto msgIt = pendingMessages_.begin(); msgIt != pendingMessages_.end();) {
        auto& pending = msgIt->second;

        for (auto pkgIt = pending.packages.begin(); pkgIt != pending.packages.end();) {
            auto& info = pkgIt->second;

            if (now - info.lastSent >= retransmitInterval_) {
                if (info.attempts >= maxRetransmitAttempts_) {
                    std::cout << "[SessionManager] Dropping package " << info.pkg.packageId
                              << " after reaching max retransmits" << std::endl;
                    packageToMessage_.erase(info.pkg.packageId);
                    pkgIt = pending.packages.erase(pkgIt);
                    continue;
                }
                sendPackageLocked(info, now);
            }
            ++pkgIt;
        }

        if (pending.packages.empty()) {
            msgIt = pendingMessages_.erase(msgIt);
        } else {
            ++msgIt;
        }
    }
}

void SessionManager::sendPackageLocked(PendingPackageInfo& info, const std::chrono::steady_clock::time_point& now) {
    outgoingPackages_.push(info.pkg);
    info.lastSent = now;
    ++info.attempts;
}

std::optional<PackageId> SessionManager::parseAckPayload(const std::string& payload) const {
    const std::string token = "\"ackPackageId\"";
    size_t keyPos = payload.find(token);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    size_t colonPos = payload.find(':', keyPos + token.size());
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }

    size_t valueStart = colonPos + 1;
    while (valueStart < payload.size() && std::isspace(static_cast<unsigned char>(payload[valueStart]))) {
        ++valueStart;
    }

    bool negative = false;
    if (valueStart < payload.size() && payload[valueStart] == '-') {
        negative = true;
        ++valueStart;
    }

    size_t valueEnd = valueStart;
    while (valueEnd < payload.size() && std::isdigit(static_cast<unsigned char>(payload[valueEnd]))) {
        ++valueEnd;
    }

    if (valueEnd == valueStart) {
        return std::nullopt;
    }

    try {
        PackageId value = static_cast<PackageId>(std::stoll(payload.substr(valueStart, valueEnd - valueStart)));
        return negative ? -value : value;
    } catch (...) {
        return std::nullopt;
    }
}

void SessionManager::handleAckPackage(const Package& pkg) {
    auto ackIdOpt = parseAckPayload(pkg.payload);
    if (!ackIdOpt.has_value()) {
        std::cout << "[SessionManager] Failed to parse ACK payload: '" << pkg.payload << "'" << std::endl;
        return;
    }

    std::function<void()> callback;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        PackageId ackId = *ackIdOpt;
        auto pkgMsgIt = packageToMessage_.find(ackId);
        if (pkgMsgIt == packageToMessage_.end()) {
            std::cout << "[SessionManager] ACK for unknown packageId=" << ackId << std::endl;
            return;
        }

        MessageId msgId = pkgMsgIt->second;
        packageToMessage_.erase(pkgMsgIt);

        auto msgIt = pendingMessages_.find(msgId);
        if (msgIt == pendingMessages_.end()) {
            return;
        }

        auto packageIt = msgIt->second.packages.find(ackId);
        if (packageIt != msgIt->second.packages.end()) {
            msgIt->second.packages.erase(packageIt);
        }

        if (msgIt->second.packages.empty()) {
            callback = msgIt->second.message.onDelivered;
            pendingMessages_.erase(msgIt);
        }
    }

    if (callback) {
        callback();
    }
}

void SessionManager::sendAckForPackageLocked(const Package& pkg) {
    Package ack{
        nextPackageId_++,
        nextAckMessageId_--,
        pkg.connId,
        0,
        1,
        std::string{},
        MessageFormat::CONFIRMATION,
        pkg.priority + 1,
        false,
        PackageStatus::QUEUED
    };

    ack.payload = std::string("{\"ackPackageId\":") + std::to_string(pkg.packageId) + "}";
    outgoingPackages_.push(ack);
}

void SessionManager::receivePackage(const Package& pkg) {
    if (pkg.format == MessageFormat::CONFIRMATION) {
        handleAckPackage(pkg);
        return;
    }

    std::cout << "[SessionManager] receivePackage: msgId=" << pkg.messageId
              << ", fragId=" << pkg.fragmentId << "/" << pkg.fragmentsCount
              << ", payload='" << pkg.payload << "'" << std::endl;

    Message messageToDeliver{};
    bool shouldDeliver = false;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);

        if (pkg.requireAck) {
            sendAckForPackageLocked(pkg);
        }

        receivedPackages_[pkg.messageId].push_back(pkg);
        auto& vec = receivedPackages_[pkg.messageId];
        std::cout << "[SessionManager] fragments received for msgId=" << pkg.messageId
                  << ": " << vec.size() << "/" << pkg.fragmentsCount << std::endl;

        if (static_cast<int>(vec.size()) < pkg.fragmentsCount) {
            return;
        }

        std::sort(vec.begin(), vec.end(), [](const Package& a, const Package& b) { return a.fragmentId < b.fragmentId; });
        std::string fullPayload;
        fullPayload.reserve(vec.size() * maxPacketSize_);

        for (int i = 0; i < pkg.fragmentsCount; ++i) {
            if (vec[i].fragmentId != i) {
                std::cout << "[SessionManager] Fragment index mismatch at i=" << i
                          << ", got " << vec[i].fragmentId << ". Returning." << std::endl;
                return;
            }
            fullPayload += vec[i].payload;
        }

        receivedPackages_.erase(pkg.messageId);

        messageToDeliver = Message{
            pkg.messageId,
            pkg.connId,
            fullPayload,
            pkg.format,
            pkg.priority,
            pkg.requireAck,
            nullptr
        };
        shouldDeliver = true;
        std::cout << "[SessionManager] All fragments received. Passing message up: '"
                  << fullPayload << "'" << std::endl;
    }

    if (shouldDeliver) {
        sdk_.onMessageReceived(messageToDeliver);
    }
}

bool SessionManager::getNextPackage(Package& out) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (outgoingPackages_.empty()) {
        return false;
    }
    out = outgoingPackages_.front();
    outgoingPackages_.pop();
    return true;
}



