#include "transport_layer.hpp"
#include "session_manager.hpp"
#include <iostream>
#include <thread>
#include <chrono>


Frame TransportLayer::serialize(const Package& pkg) {
    Frame frame;
    appendBytes(frame.data, pkg.packageId, 4); // uint32_t
    appendBytes(frame.data, pkg.messageId, 4); // uint32_t
    appendBytes(frame.data, pkg.connId, 4); // uint32_t
    appendBytes(frame.data, pkg.fragmentId, 2); // uint16_t
    appendBytes(frame.data, pkg.fragmentsCount, 2); // uint16_t
    appendBytes(frame.data, static_cast<uint8_t>(pkg.format), 1); // uint8_t
    appendBytes(frame.data, static_cast<uint8_t>(pkg.priority), 1); // uint8_t
    appendBytes(frame.data, static_cast<uint8_t>(pkg.requireAck ? 1 : 0), 1); // uint8_t
    appendBytes(frame.data, static_cast<uint16_t>(pkg.payload.size()), 2); // uint16_t
    for (char c : pkg.payload) frame.data.push_back(static_cast<uint8_t>(c));
    return frame;
}

void TransportLayer::appendBytes(std::vector<uint8_t>& bytes, uint64_t value, int byteCount) {
    for (int i = byteCount - 1; i >= 0; --i) {
        bytes.push_back((value >> (i * 8)) & 0xFF);
    }
}

uint64_t TransportLayer::readBytes(const std::vector<uint8_t>& bytes, size_t& offset, int byteCount) {
    uint64_t value = 0;
    for (int i = 0; i < byteCount; i++) {
        value = (value << 8) | bytes[offset++];
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
    std::cout << "[TransportLayer] Received frame, deserialized package: id=" << pkg.packageId
              << ", msgId=" << pkg.messageId
              << ", frag=" << pkg.fragmentId << "/" << pkg.fragmentsCount
              << ", payload='" << pkg.payload << "'\n";
    sessionManager_.receivePackage(pkg);
}

// Przykładowa prosta deserializacja (musi być zgodna z serialize)
Package TransportLayer::deserialize(const Frame& frame) {
    size_t offset = 0;
    auto& data = frame.data;
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

std::queue<Frame>& TransportLayer::getOutgoingFrames() {
    return outgoingFrames_;
}

