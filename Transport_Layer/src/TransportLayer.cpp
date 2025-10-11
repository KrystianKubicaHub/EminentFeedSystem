#include "TransportLayer.hpp"
#include "SessionManager.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace std;
using namespace chrono;

TransportLayer::TransportLayer(queue<Package>& outgoingPackages, SessionManager& sessionManager, const ValidationConfig& validationConfig)
        : LoggerBase("TransportLayer"),
            outgoingPackages_(outgoingPackages),
            sessionManager_(sessionManager),
            validationConfig_(validationConfig) {
        initializeFieldWidths();
    worker_ = thread([this]() { workerLoop(); });
}

TransportLayer::~TransportLayer() {
    stopWorker_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TransportLayer::workerLoop() {
    while (!stopWorker_) {
        while (!outgoingPackages_.empty()) {
            Package pkg = outgoingPackages_.front();
            outgoingPackages_.pop();

            Frame frame = serialize(pkg);
            outgoingFrames_.push(frame);

            ostringstream oss;
            oss << "Queued package id=" << pkg.packageId
                << " msgId=" << pkg.messageId
                << " fragment=" << pkg.fragmentId << '/' << pkg.fragmentsCount
                << " payload='" << pkg.payload << "' size=" << frame.data.size();
            log(LogLevel::DEBUG, oss.str());

            ostringstream bytesOss;
            for (size_t i = 0; i < min<size_t>(8, frame.data.size()); ++i) {
                bytesOss << hex << static_cast<int>(frame.data[i]) << ' ';
            }
            if (!frame.data.empty()) {
                log(LogLevel::DEBUG, string("Frame first bytes: ") + bytesOss.str());
            }
        }
        this_thread::sleep_for(10ms);
    }
}

Frame TransportLayer::serialize(const Package& pkg) {
    validateSerializedPackage(pkg);

    Frame frame;
    appendBytes(frame.data, static_cast<uint64_t>(pkg.packageId), packageIdBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.messageId), messageIdBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.connId), connectionIdBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.fragmentId), fragmentIdBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.fragmentsCount), fragmentsCountBytes_);
    appendBytes(frame.data, static_cast<uint8_t>(pkg.format), formatBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.priority), priorityBytes_);
    appendBytes(frame.data, static_cast<uint8_t>(pkg.requireAck ? 1 : 0), requireAckBytes_);
    appendBytes(frame.data, static_cast<uint64_t>(pkg.payload.size()), payloadLengthBytes_);
    for (char c : pkg.payload) {
        frame.data.push_back(static_cast<uint8_t>(c));
    }
    return frame;
}

void TransportLayer::appendBytes(vector<uint8_t>& bytes, uint64_t value, int byteCount) {
    if (byteCount <= 0) {
        throw runtime_error("appendBytes called with non-positive byteCount");
    }
    for (int i = byteCount - 1; i >= 0; --i) {
        bytes.push_back((value >> (i * 8)) & 0xFF);
    }
}

uint64_t TransportLayer::readBytes(const vector<uint8_t>& bytes, size_t& offset, int byteCount) {
    if (byteCount <= 0) {
        throw runtime_error("readBytes called with non-positive byteCount");
    }
    if (offset + static_cast<size_t>(byteCount) > bytes.size()) {
        throw runtime_error("Frame truncated while reading bytes");
    }
    uint64_t value = 0;
    for (int i = 0; i < byteCount; ++i) {
        value = (value << 8) | bytes[offset++];
    }
    return value;
}

uint32_t TransportLayer::crc32(const vector<uint8_t>&) {
    return 0;
}

void TransportLayer::receiveFrame(const Frame& frame) {
    Package pkg = deserialize(frame);
    ostringstream oss;
    oss << "Received frame -> package id=" << pkg.packageId
        << " msgId=" << pkg.messageId
        << " fragment=" << pkg.fragmentId << '/' << pkg.fragmentsCount
        << " payload='" << pkg.payload << "'";
    log(LogLevel::DEBUG, oss.str());
    sessionManager_.receivePackage(pkg);
}

Package TransportLayer::deserialize(const Frame& frame) {
    size_t offset = 0;
    const auto& data = frame.data;
    Package pkg;
    pkg.packageId = static_cast<int>(readBytes(data, offset, packageIdBytes_));
    pkg.messageId = static_cast<int>(readBytes(data, offset, messageIdBytes_));
    pkg.connId = static_cast<int>(readBytes(data, offset, connectionIdBytes_));
    pkg.fragmentId = static_cast<int>(readBytes(data, offset, fragmentIdBytes_));
    pkg.fragmentsCount = static_cast<int>(readBytes(data, offset, fragmentsCountBytes_));
    pkg.format = static_cast<MessageFormat>(readBytes(data, offset, formatBytes_));
    pkg.priority = static_cast<int>(readBytes(data, offset, priorityBytes_));
    pkg.requireAck = readBytes(data, offset, requireAckBytes_) != 0;
    uint64_t payloadSize64 = readBytes(data, offset, payloadLengthBytes_);
    if (payloadSize64 > numeric_limits<size_t>::max()) {
        throw runtime_error("Payload length exceeds platform limits");
    }
    size_t payloadSize = static_cast<size_t>(payloadSize64);
    if (offset + payloadSize > data.size()) {
        throw runtime_error("Frame truncated while reading payload");
    }
    pkg.payload.clear();
    pkg.payload.reserve(payloadSize);
    for (size_t i = 0; i < payloadSize; ++i) {
        pkg.payload.push_back(static_cast<char>(data[offset++]));
    }
    pkg.status = PackageStatus::QUEUED;
    validateDeserializedPackage(pkg);
    return pkg;
}

void TransportLayer::initializeFieldWidths() {
    auto bitsToMax = [](uint8_t bits) -> uint64_t {
        return bits >= 32 ? numeric_limits<uint32_t>::max() : ((1ULL << bits) - 1ULL);
    };

    auto bitsToBytes = [](uint8_t bits) -> uint8_t {
        uint8_t bytes = static_cast<uint8_t>((bits + 7) / 8);
        return bytes == 0 ? static_cast<uint8_t>(1) : bytes;
    };

    packageIdMax_ = bitsToMax(validationConfig_.packageIdBitWidth());
    messageIdMax_ = bitsToMax(validationConfig_.messageIdBitWidth());
    connectionIdMax_ = bitsToMax(validationConfig_.connectionIdBitWidth());
    fragmentIdMax_ = bitsToMax(validationConfig_.fragmentIdBitWidth());
    fragmentsCountMax_ = bitsToMax(validationConfig_.fragmentsCountBitWidth());
    priorityMax_ = bitsToMax(validationConfig_.priorityBitWidth());

    packageIdBytes_ = bitsToBytes(validationConfig_.packageIdBitWidth());
    messageIdBytes_ = bitsToBytes(validationConfig_.messageIdBitWidth());
    connectionIdBytes_ = bitsToBytes(validationConfig_.connectionIdBitWidth());
    fragmentIdBytes_ = bitsToBytes(validationConfig_.fragmentIdBitWidth());
    fragmentsCountBytes_ = bitsToBytes(validationConfig_.fragmentsCountBitWidth());
    priorityBytes_ = bitsToBytes(validationConfig_.priorityBitWidth());

    requireAckBytes_ = 1;
    formatBytes_ = 1;
    payloadLengthBytes_ = 2;
}

void TransportLayer::validateSerializedPackage(const Package& pkg) const {
    validationConfig_.validatePackage(pkg);

    auto exceeds = [](uint64_t value, uint64_t maxAllowed) {
        return value > maxAllowed;
    };

    if (exceeds(static_cast<uint64_t>(pkg.packageId), packageIdMax_) ||
        exceeds(static_cast<uint64_t>(pkg.messageId), messageIdMax_) ||
        exceeds(static_cast<uint64_t>(pkg.connId), connectionIdMax_) ||
        exceeds(static_cast<uint64_t>(pkg.fragmentId), fragmentIdMax_) ||
        exceeds(static_cast<uint64_t>(pkg.fragmentsCount), fragmentsCountMax_) ||
        exceeds(static_cast<uint64_t>(pkg.priority), priorityMax_)) {
        throw runtime_error("Package fields exceed allowed encoding width");
    }

    if (pkg.payload.size() >= (1ULL << (payloadLengthBytes_ * 8))) {
        throw runtime_error("Payload too large to encode");
    }
}

void TransportLayer::validateDeserializedPackage(const Package& pkg) const {
    validationConfig_.validatePackage(pkg);
}

queue<Frame>& TransportLayer::getOutgoingFrames() {
    return outgoingFrames_;
}

