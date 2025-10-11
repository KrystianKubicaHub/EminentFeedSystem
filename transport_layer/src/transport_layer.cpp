#include "transport_layer.hpp"
#include "session_manager.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

using namespace std;
using namespace chrono;

TransportLayer::TransportLayer(queue<Package>& outgoingPackages, SessionManager& sessionManager)
    : LoggerBase("TransportLayer"), outgoingPackages_(outgoingPackages), sessionManager_(sessionManager) {
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
    Frame frame;
    appendBytes(frame.data, pkg.packageId, 4);
    appendBytes(frame.data, pkg.messageId, 4);
    appendBytes(frame.data, pkg.connId, 4);
    appendBytes(frame.data, pkg.fragmentId, 2);
    appendBytes(frame.data, pkg.fragmentsCount, 2);
    appendBytes(frame.data, static_cast<uint8_t>(pkg.format), 1);
    appendBytes(frame.data, static_cast<uint8_t>(pkg.priority), 1);
    appendBytes(frame.data, static_cast<uint8_t>(pkg.requireAck ? 1 : 0), 1);
    appendBytes(frame.data, static_cast<uint16_t>(pkg.payload.size()), 2);
    for (char c : pkg.payload) {
        frame.data.push_back(static_cast<uint8_t>(c));
    }
    return frame;
}

void TransportLayer::appendBytes(vector<uint8_t>& bytes, uint64_t value, int byteCount) {
    for (int i = byteCount - 1; i >= 0; --i) {
        bytes.push_back((value >> (i * 8)) & 0xFF);
    }
}

uint64_t TransportLayer::readBytes(const vector<uint8_t>& bytes, size_t& offset, int byteCount) {
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
    pkg.packageId = readBytes(data, offset, 4);
    pkg.messageId = readBytes(data, offset, 4);
    pkg.connId = readBytes(data, offset, 4);
    pkg.fragmentId = readBytes(data, offset, 2);
    pkg.fragmentsCount = readBytes(data, offset, 2);
    pkg.format = static_cast<MessageFormat>(readBytes(data, offset, 1));
    pkg.priority = readBytes(data, offset, 1);
    pkg.requireAck = readBytes(data, offset, 1) != 0;
    size_t payloadSize = readBytes(data, offset, 2);
    pkg.payload.clear();
    for (size_t i = 0; i < payloadSize && offset < data.size(); ++i) {
        pkg.payload.push_back(static_cast<char>(data[offset++]));
    }
    pkg.status = PackageStatus::QUEUED;
    return pkg;
}

queue<Frame>& TransportLayer::getOutgoingFrames() {
    return outgoingFrames_;
}

