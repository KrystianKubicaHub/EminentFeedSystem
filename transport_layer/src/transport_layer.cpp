#include "transport_layer.hpp"
#include "session_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>


Frame TransportLayer::serialize(const Package& pkg) {
    Frame frame;
    appendBits(frame.data, pkg.packageId, 32);
    appendBits(frame.data, pkg.messageId, 32);
    appendBits(frame.data, pkg.connId, 32);
    appendBits(frame.data, pkg.fragmentId, 16);
    appendBits(frame.data, pkg.fragmentsCount, 16);
    appendBits(frame.data, static_cast<uint64_t>(pkg.format), 8);
    appendBits(frame.data, pkg.priority, 8);
    appendBits(frame.data, pkg.requireAck ? 1 : 0, 1);
    appendBits(frame.data, pkg.payload.size(), 16);
    for (char c : pkg.payload) frame.data.push_back(static_cast<uint8_t>(c));
    return frame;
}

void TransportLayer::appendBits(std::vector<uint8_t>& bits, uint64_t value, int bitCount) {
    for (int i = bitCount - 1; i >= 0; --i) {
        bits.push_back((value >> i) & 0xFF);
    }
}

uint64_t TransportLayer::readBits(const std::vector<uint8_t>& bits, size_t& offset, int bitCount) {
    uint64_t value = 0;
    for (int i = 0; i < bitCount; i++) {
        value = (value << 8) | bits[offset++];
    }
    return value;
}

uint32_t TransportLayer::crc32(const std::vector<uint8_t>& dataBytes) {
    // Placeholder, nieużywane na tym etapie
    return 0;
}


TransportLayer::TransportLayer(std::queue<Package>& outgoingPackages, SessionManager& sessionManager)
    : outgoingPackages_(outgoingPackages), sessionManager_(sessionManager) {
    std::thread([this]() {
        while (true) {
            while (!outgoingPackages_.empty()) {
                Package pkg = outgoingPackages_.front();
                outgoingPackages_.pop();
                std::cout << "[TransportLayer] Package: id=" << pkg.packageId
                          << ", msgId=" << pkg.messageId
                          << ", frag=" << pkg.fragmentId << "/" << pkg.fragmentsCount
                          << ", payload='" << pkg.payload << "'\n";
                Frame frame = serialize(pkg);
                outgoingFrames_.push(frame);
                std::cout << "[TransportLayer] Frame added: size=" << frame.data.size() << " bytes, first 8 bytes: ";
                for (size_t i = 0; i < std::min<size_t>(8, frame.data.size()); ++i) {
                    std::cout << std::hex << (int)frame.data[i] << " ";
                }
                std::cout << std::dec << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}

void TransportLayer::receiveFrame(const Frame& frame) {
    Package pkg = deserialize(frame);
    sessionManager_.receivePackage(pkg);
}

// Przykładowa prosta deserializacja (musi być zgodna z serialize)
Package TransportLayer::deserialize(const Frame& frame) {
    size_t offset = 0;
    auto& data = frame.data;
    Package pkg;
    pkg.packageId = readBits(data, offset, 32);
    pkg.messageId = readBits(data, offset, 32);
    pkg.connId = readBits(data, offset, 32);
    pkg.fragmentId = readBits(data, offset, 16);
    pkg.fragmentsCount = readBits(data, offset, 16);
    pkg.format = static_cast<MessageFormat>(readBits(data, offset, 8));
    pkg.priority = readBits(data, offset, 8);
    pkg.requireAck = readBits(data, offset, 1) != 0;
    size_t payloadSize = readBits(data, offset, 16);
    pkg.payload.clear();
    for (size_t i = 0; i < payloadSize && offset < data.size(); ++i) {
        pkg.payload.push_back(static_cast<char>(data[offset++]));
    }
    pkg.status = PackageStatus::QUEUED;
    return pkg;
}

std::queue<Frame>& TransportLayer::getOutgoingFrames() {
    return outgoingFrames_;
}

