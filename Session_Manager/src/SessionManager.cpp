#include "SessionManager.hpp"
#include "EminentSdk.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <utility>
#include <mutex>
#include <thread>

using namespace std;
using namespace chrono;

SessionManager::SessionManager(queue<Message>& sdkQueue, EminentSdk& sdk, size_t maxPacketSize)
    : LoggerBase("SessionManager"), sdkQueue_(sdkQueue), sdk_(sdk), maxPacketSize_(maxPacketSize) {
    worker_ = thread([this]() { workerLoop(); });
}

SessionManager::~SessionManager() {
    {
        lock_guard<mutex> lock(queueMutex_);
        stopWorker_ = true;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void SessionManager::workerLoop() {
    vector<function<void()>> callbacks;
    while (true) {
        callbacks.clear();
        auto now = steady_clock::now();
        {
            lock_guard<mutex> lock(queueMutex_);
            if (stopWorker_) {
                break;
            }
            processSdkQueueLocked(now, callbacks);
            retransmitPendingLocked(now);
        }
        for (auto& cb : callbacks) {
            if (cb) {
                cb();
            }
        }
        this_thread::sleep_for(workerSleepInterval_);
    }
}

void SessionManager::processMessages() {
    auto now = steady_clock::now();
    vector<function<void()>> callbacks;
    {
        lock_guard<mutex> lock(queueMutex_);
        processSdkQueueLocked(now, callbacks);
        retransmitPendingLocked(now);
    }
    for (auto& cb : callbacks) {
        if (cb) {
            cb();
        }
    }
}

void SessionManager::processSdkQueueLocked(const steady_clock::time_point& now, vector<function<void()>>& callbacks) {
    while (!sdkQueue_.empty()) {
        Message msg = sdkQueue_.front();
        sdkQueue_.pop();

        int total = static_cast<int>((msg.payload.size() + maxPacketSize_ - 1) / maxPacketSize_);
        if (total <= 0) {
            total = 1;
        }

        bool trackForAck = msg.requireAck;
        PendingMessageInfo pending;
        if (trackForAck) {
            pending.message = msg;
        }

        for (int frag = 0; frag < total; ++frag) {
            string fragment = msg.payload.substr(static_cast<size_t>(frag) * maxPacketSize_, maxPacketSize_);
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

            if (trackForAck) {
                pending.packages.emplace(pkg.packageId, info);
                packageToMessage_[pkg.packageId] = msg.id;
            }
        }

        if (trackForAck) {
            pendingMessages_[msg.id] = move(pending);
        } else if (msg.onDelivered) {
            callbacks.push_back(msg.onDelivered);
        }
    }
}

void SessionManager::retransmitPendingLocked(const steady_clock::time_point& now) {
    for (auto msgIt = pendingMessages_.begin(); msgIt != pendingMessages_.end();) {
        auto& pending = msgIt->second;

        for (auto pkgIt = pending.packages.begin(); pkgIt != pending.packages.end();) {
            auto& info = pkgIt->second;

            if (now - info.lastSent >= retransmitInterval_) {
                if (info.attempts >= maxRetransmitAttempts_) {
                    log(LogLevel::WARN, string("Dropping package ") + to_string(info.pkg.packageId) +
                            " after reaching max retransmits");
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

void SessionManager::sendPackageLocked(PendingPackageInfo& info, const steady_clock::time_point& now) {
    outgoingPackages_.push(info.pkg);
    info.lastSent = now;
    ++info.attempts;
}

optional<PackageId> SessionManager::parseAckPayload(const string& payload) const {
    const string token = "\"ackPackageId\"";
    size_t keyPos = payload.find(token);
    if (keyPos == string::npos) {
        return nullopt;
    }

    size_t colonPos = payload.find(':', keyPos + token.size());
    if (colonPos == string::npos) {
        return nullopt;
    }

    size_t valueStart = colonPos + 1;
    while (valueStart < payload.size() && isspace(static_cast<unsigned char>(payload[valueStart]))) {
        ++valueStart;
    }

    bool negative = false;
    if (valueStart < payload.size() && payload[valueStart] == '-') {
        negative = true;
        ++valueStart;
    }

    size_t valueEnd = valueStart;
    while (valueEnd < payload.size() && isdigit(static_cast<unsigned char>(payload[valueEnd]))) {
        ++valueEnd;
    }

    if (valueEnd == valueStart) {
        return nullopt;
    }

    try {
        PackageId value = static_cast<PackageId>(stoll(payload.substr(valueStart, valueEnd - valueStart)));
        return negative ? -value : value;
    } catch (...) {
        return nullopt;
    }
}

void SessionManager::handleAckPackage(const Package& pkg) {
    auto ackIdOpt = parseAckPayload(pkg.payload);
    if (!ackIdOpt.has_value()) {
        log(LogLevel::WARN, string("Failed to parse ACK payload: '") + pkg.payload + "'");
        return;
    }

    function<void()> callback;

    {
        lock_guard<mutex> lock(queueMutex_);

        PackageId ackId = *ackIdOpt;
        auto pkgMsgIt = packageToMessage_.find(ackId);
        if (pkgMsgIt == packageToMessage_.end()) {
            log(LogLevel::WARN, string("ACK for unknown packageId=") + to_string(ackId));
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
        string{},
        MessageFormat::CONFIRMATION,
        pkg.priority + 1,
        false,
        PackageStatus::QUEUED
    };

    ack.payload = string("{\"ackPackageId\":") + to_string(pkg.packageId) + "}";
    outgoingPackages_.push(ack);
}

void SessionManager::receivePackage(const Package& pkg) {
    if (pkg.format == MessageFormat::CONFIRMATION) {
        handleAckPackage(pkg);
        return;
    }

    log(LogLevel::DEBUG, string("receivePackage: msgId=") + to_string(pkg.messageId) +
            ", fragId=" + to_string(pkg.fragmentId) + "/" + to_string(pkg.fragmentsCount) +
            ", payload='" + pkg.payload + "'");

    Message messageToDeliver{};
    bool shouldDeliver = false;

    {
        lock_guard<mutex> lock(queueMutex_);

        if (pkg.requireAck) {
            sendAckForPackageLocked(pkg);
        }

        receivedPackages_[pkg.messageId].push_back(pkg);
        auto& vec = receivedPackages_[pkg.messageId];
        log(LogLevel::DEBUG, string("Fragments received for msgId=") + to_string(pkg.messageId) +
                ": " + to_string(vec.size()) + "/" + to_string(pkg.fragmentsCount));

        if (static_cast<int>(vec.size()) < pkg.fragmentsCount) {
            return;
        }

        sort(vec.begin(), vec.end(), [](const Package& a, const Package& b) { return a.fragmentId < b.fragmentId; });
        string fullPayload;
        fullPayload.reserve(vec.size() * maxPacketSize_);

        for (int i = 0; i < pkg.fragmentsCount; ++i) {
            if (vec[i].fragmentId != i) {
                log(LogLevel::WARN, string("Fragment index mismatch at i=") + to_string(i) +
                        ", got " + to_string(vec[i].fragmentId));
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
        log(LogLevel::DEBUG, string("All fragments received. Passing message up: '") + fullPayload + "'");
    }

    if (shouldDeliver) {
        sdk_.onMessageReceived(messageToDeliver);
    }
}

bool SessionManager::getNextPackage(Package& out) {
    lock_guard<mutex> lock(queueMutex_);
    if (outgoingPackages_.empty()) {
        return false;
    }
    out = outgoingPackages_.front();
    outgoingPackages_.pop();
    return true;
}



