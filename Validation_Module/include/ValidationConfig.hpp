#pragma once

#include <cstddef>
#include <cstdint>
#include <commonTypes.hpp>

using namespace std;

class ValidationConfig {
public:
    static constexpr size_t FORMAT_FIELD_BYTES = 1;
    static constexpr size_t REQUIRE_ACK_FIELD_BYTES = 1;
    static constexpr size_t PAYLOAD_LENGTH_FIELD_BYTES = 2;
    static constexpr size_t CRC_FIELD_BYTES = 4;

    static constexpr uint8_t DEFAULT_DEVICE_ID_BITS = 16;
    static constexpr uint8_t DEFAULT_CONNECTION_ID_BITS = 16;
    static constexpr uint8_t DEFAULT_MESSAGE_ID_BITS = 24;
    static constexpr uint8_t DEFAULT_PACKAGE_ID_BITS = 24;
    static constexpr uint8_t DEFAULT_FRAGMENT_ID_BITS = 8;
    static constexpr uint8_t DEFAULT_FRAGMENTS_COUNT_BITS = 8;
    static constexpr uint8_t DEFAULT_PRIORITY_BITS = 4;
    static constexpr uint8_t DEFAULT_SPECIAL_CODE_BITS = 16;

    ValidationConfig(
        uint8_t deviceIdBits = DEFAULT_DEVICE_ID_BITS,
        uint8_t connectionIdBits = DEFAULT_CONNECTION_ID_BITS,
        uint8_t messageIdBits = DEFAULT_MESSAGE_ID_BITS,
        uint8_t packageIdBits = DEFAULT_PACKAGE_ID_BITS,
        uint8_t fragmentIdBits = DEFAULT_FRAGMENT_ID_BITS,
        uint8_t fragmentsCountBits = DEFAULT_FRAGMENTS_COUNT_BITS,
        uint8_t priorityBits = DEFAULT_PRIORITY_BITS,
        uint8_t specialCodeBits = DEFAULT_SPECIAL_CODE_BITS
    );

    bool validateMessage(const Message& message) const;
    bool validatePackage(const Package& package) const;
    bool validateDeviceId(DeviceId deviceId) const;
    bool validateConnectionId(ConnectionId connectionId) const;
    bool validateMessageId(MessageId messageId) const;
    bool validatePackageId(PackageId packageId) const;
    bool validatePriority(Priority priority) const;
    bool validateSpecialCode(int specialCode) const;

    uint8_t deviceIdBitWidth() const { return deviceIdBits_; }
    uint8_t connectionIdBitWidth() const { return connectionIdBits_; }
    uint8_t messageIdBitWidth() const { return messageIdBits_; }
    uint8_t packageIdBitWidth() const { return packageIdBits_; }
    uint8_t fragmentIdBitWidth() const { return fragmentIdBits_; }
    uint8_t fragmentsCountBitWidth() const { return fragmentsCountBits_; }
    uint8_t priorityBitWidth() const { return priorityBits_; }
    uint8_t specialCodeBitWidth() const { return specialCodeBits_; }

    size_t transportHeaderBytes() const;
    size_t maxPayloadLengthBytes() const;
    size_t maxFrameLengthBytes() const;

private:
    bool fitsInBits(int value, uint8_t bits) const;
    void validateBits(uint8_t bits) const;
    size_t bitsToBytes(uint8_t bits) const;

    const uint8_t deviceIdBits_;
    const uint8_t connectionIdBits_;
    const uint8_t messageIdBits_;
    const uint8_t packageIdBits_;
    const uint8_t fragmentIdBits_;
    const uint8_t fragmentsCountBits_;
    const uint8_t priorityBits_;
    const uint8_t specialCodeBits_;
};
