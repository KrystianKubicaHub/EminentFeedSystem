#include "transport_layer.hpp"
#include <iostream>
#include <cstring>

uint32_t TransportLayer::crc32(const std::vector<uint8_t>& dataBytes) {
    uint32_t crc = 0xFFFFFFFF;
    for (auto byte : dataBytes) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

void TransportLayer::appendBits(std::vector<uint8_t>& bits, uint64_t value, int bitCount) {
    for (int i = bitCount - 1; i >= 0; --i) {
        bits.push_back((value >> i) & 1);
    }
}

uint64_t TransportLayer::readBits(const std::vector<uint8_t>& bits, size_t& offset, int bitCount) {
    uint64_t value = 0;
    for (int i = 0; i < bitCount; i++) {
        value = (value << 1) | bits[offset++];
    }
    return value;
}

Frame TransportLayer::serialize(const Package& pkg) {
    Frame frame;
    auto& bits = frame.bits;

    appendBits(bits, 0xAA, 8);

    appendBits(bits, pkg.senderId, 16);
    appendBits(bits, pkg.receiverId, 16);
    appendBits(bits, pkg.messageId, 32);
    appendBits(bits, pkg.packageId, 32);
    appendBits(bits, static_cast<uint8_t>(pkg.format), 8);
    appendBits(bits, pkg.priority, 8);

    uint8_t flags = pkg.requireAck ? 1 : 0;
    appendBits(bits, flags, 8);

    appendBits(bits, pkg.payload.size(), 32);

    std::vector<uint8_t> payloadBytes(pkg.payload.begin(), pkg.payload.end());
    for (uint8_t b : payloadBytes) {
        appendBits(bits, b, 8);
    }

    uint32_t crc = crc32(payloadBytes);
    appendBits(bits, crc, 32);

    return frame;
}

Package TransportLayer::deserialize(const Frame& frame) {
    const auto& bits = frame.bits;
    if (bits.size() < 12 * 8) {
        throw std::runtime_error("Frame too short");
    }

    Package pkg{};
    size_t offset = 0;

    uint64_t sync = readBits(bits, offset, 8);
    if (sync != 0xAA) {
        throw std::runtime_error("Invalid sync byte");
    }

    pkg.senderId   = readBits(bits, offset, 16);
    pkg.receiverId = readBits(bits, offset, 16);
    pkg.messageId  = readBits(bits, offset, 32);
    pkg.packageId  = readBits(bits, offset, 32);
    pkg.format     = static_cast<MessageFormat>(readBits(bits, offset, 8));
    pkg.priority   = readBits(bits, offset, 8);

    uint8_t flags  = readBits(bits, offset, 8);
    pkg.requireAck = (flags & 1);

    size_t payloadLen = readBits(bits, offset, 32);

    std::string payload;
    for (size_t i = 0; i < payloadLen; i++) {
        payload.push_back(static_cast<char>(readBits(bits, offset, 8)));
    }
    pkg.payload = payload;

    uint32_t receivedCrc = readBits(bits, offset, 32);
    uint32_t computedCrc = crc32(std::vector<uint8_t>(payload.begin(), payload.end()));

    if (receivedCrc != computedCrc) {
        throw std::runtime_error("CRC mismatch");
    }

    return pkg;
}
