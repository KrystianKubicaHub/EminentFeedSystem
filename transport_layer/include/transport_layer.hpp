#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

using DeviceId = int;
using MessageId = int;
using PackageId = int;

enum class MessageFormat : uint8_t {
    JSON = 0,
    VIDEO = 1,
    HANDSHAKE = 2
};

struct Package {
    PackageId packageId;
    MessageId messageId;
    DeviceId senderId;
    DeviceId receiverId;
    MessageFormat format;
    bool requireAck;
    int priority;
    std::string payload;
};

struct Frame {
    std::vector<uint8_t> bits;
};

class TransportLayer {
public:
    Frame serialize(const Package& pkg);

    Package deserialize(const Frame& frame);

private:
    void appendBits(std::vector<uint8_t>& bits, uint64_t value, int bitCount);
    uint64_t readBits(const std::vector<uint8_t>& bits, size_t& offset, int bitCount);

    uint32_t crc32(const std::vector<uint8_t>& dataBytes);
};
