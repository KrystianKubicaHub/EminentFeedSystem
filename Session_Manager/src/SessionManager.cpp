#include "SessionManager.hpp"
#include "EminentSdk.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <limits>
#include <utility>
#include <mutex>
#include <thread>
#include <stdexcept>

using namespace std;
using namespace chrono;

SessionManager::SessionManager(queue<Message>& sdkQueue, EminentSdk& sdk, const ValidationConfig& validationConfig, size_t maxPacketSize)
    : LoggerBase("SessionManager"),
      sdkQueue_(sdkQueue),
      sdk_(sdk),
            validationConfig_(validationConfig),
            maxPacketSize_(maxPacketSize) {
        if (maxPacketSize_ == 0) {
                throw invalid_argument("SessionManager requires positive maxPacketSize");
        }
    maxPackageIdValue_ = maxValueForBits(validationConfig_.packageIdBitWidth());
    maxMessageIdValue_ = maxValueForBits(validationConfig_.messageIdBitWidth());
    maxFragmentIdValue_ = maxValueForBits(validationConfig_.fragmentIdBitWidth());
    maxFragmentsCountValue_ = maxValueForBits(validationConfig_.fragmentsCountBitWidth());
    maxPriorityValue_ = maxValueForBits(validationConfig_.priorityBitWidth());

    if (maxFragmentsCountValue_ == 0) {
        throw invalid_argument("ValidationConfig fragments count bits must allow at least one fragment");
    }

    nextAckMessageId_ = static_cast<MessageId>(maxMessageIdValue_);

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

        try {
            validationConfig_.validateMessage(msg);
        } catch (const exception& ex) {
            log(LogLevel::WARN, string("Dropping message due to validation failure: ") + ex.what());
            continue;
        }

        int total = static_cast<int>((msg.payload.size() + maxPacketSize_ - 1) / maxPacketSize_);
        if (total <= 0) {
            total = 1;
        }

        if (!ensureFragmentsFit(total)) {
            log(LogLevel::WARN, string("Dropping message id=") + to_string(msg.id) +
                " because fragments exceed configured bit width");
            if (msg.onDelivered) {
                callbacks.push_back([cb = msg.onDelivered]() { if (cb) cb(); });
            }
            continue;
        }

        bool trackForAck = msg.requireAck;
        PendingMessageInfo pending;
        if (trackForAck) {
            pending.message = msg;
        }

        for (int frag = 0; frag < total; ++frag) {
            if (static_cast<uint64_t>(frag) > maxFragmentIdValue_) {
                log(LogLevel::WARN, string("Fragment index ") + to_string(frag) +
                        " exceeds configured bit width; dropping message id=" + to_string(msg.id));
                trackForAck = false;
                pending.packages.clear();
                break;
            }

            string fragment = msg.payload.substr(static_cast<size_t>(frag) * maxPacketSize_, maxPacketSize_);
            Package pkg{
                allocatePackageId(),
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

            try {
                validationConfig_.validatePackage(pkg);
            } catch (const exception& ex) {
                log(LogLevel::WARN, string("Package validation failed: ") + ex.what());
                trackForAck = false;
                pending.packages.clear();
                break;
            }

            PendingPackageInfo info;
            info.pkg = pkg;
            try {
                sendPackageLocked(info, now);
            } catch (const exception& ex) {
                log(LogLevel::WARN, string("Failed to send package: ") + ex.what());
                trackForAck = false;
                pending.packages.clear();
                break;
            }

            if (trackForAck) {
                pending.packages.emplace(pkg.packageId, info);
                packageToMessage_[pkg.packageId] = msg.id;
            }
        }

        if (trackForAck && !pending.packages.empty()) {
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
                try {
                    sendPackageLocked(info, now);
                } catch (const exception& ex) {
                    log(LogLevel::WARN, string("Failed to retransmit package: ") + ex.what());
                    packageToMessage_.erase(info.pkg.packageId);
                    pkgIt = pending.packages.erase(pkgIt);
                    continue;
                }
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
    try {
        validationConfig_.validatePackage(info.pkg);
    } catch (const exception& ex) {
        throw runtime_error(string("Cannot send package: ") + ex.what());
    }
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

    if (valueStart < payload.size() && payload[valueStart] == '-') {
        return nullopt;
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
        if (value <= 0) {
            return nullopt;
        }
        return value;
    } catch (...) {
        return nullopt;
    }
}

void SessionManager::handleAckPackage(const Package& pkg) {
    try {
        validationConfig_.validatePackage(pkg);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Ignoring invalid ACK package: ") + ex.what());
        return;
    }

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
    try {
        Priority ackPriority = static_cast<Priority>(min<uint64_t>(static_cast<uint64_t>(pkg.priority) + 1ULL, maxPriorityValue_));
        validationConfig_.validatePriority(ackPriority);

        Package ack{
            allocatePackageId(),
            allocateAckMessageId(),
            pkg.connId,
            0,
            1,
            string{},
            MessageFormat::CONFIRMATION,
            ackPriority,
            false,
            PackageStatus::QUEUED
        };

        ack.payload = string("{\"ackPackageId\":") + to_string(pkg.packageId) + "}";
        validationConfig_.validatePackage(ack);
        outgoingPackages_.push(ack);
    } catch (const exception& ex) {
        log(LogLevel::WARN, string("Failed to enqueue ACK package: ") + ex.what());
    }
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

PackageId SessionManager::allocatePackageId() {
    if (static_cast<uint64_t>(nextPackageId_) > maxPackageIdValue_) {
        throw runtime_error("Package id overflow: exceeds configured bit width");
    }
    PackageId id = nextPackageId_;
    ++nextPackageId_;
    validationConfig_.validatePackageId(id);
    return id;
}

MessageId SessionManager::allocateAckMessageId() {
    if (nextAckMessageId_ <= 0) {
        throw runtime_error("Ack message id underflow: exceeds configured bit width");
    }
    MessageId id = nextAckMessageId_;
    --nextAckMessageId_;
    validationConfig_.validateMessageId(id);
    return id;
}

uint64_t SessionManager::maxValueForBits(uint8_t bits) const {
    if (bits == 0) {
        throw invalid_argument("ValidationConfig bit width cannot be zero");
    }
    uint64_t maxValue;
    if (bits >= 32) {
        maxValue = numeric_limits<uint32_t>::max();
    } else {
        maxValue = (1ULL << bits) - 1ULL;
    }
    return min<uint64_t>(maxValue, static_cast<uint64_t>(numeric_limits<int>::max()));
}

bool SessionManager::ensureFragmentsFit(int total) const {
    if (total <= 0) {
        return false;
    }
    if (static_cast<uint64_t>(total) > maxFragmentsCountValue_) {
        return false;
    }
    if (static_cast<uint64_t>(total - 1) > maxFragmentIdValue_) {
        return false;
    }
    return true;
}



