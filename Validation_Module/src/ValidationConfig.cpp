#include "ValidationConfig.hpp"
#include <limits>
#include <stdexcept>
#include <string>

using namespace std;

ValidationConfig::ValidationConfig(
    uint8_t deviceIdBits,
    uint8_t connectionIdBits,
    uint8_t messageIdBits,
    uint8_t packageIdBits,
    uint8_t fragmentIdBits,
    uint8_t fragmentsCountBits,
    uint8_t priorityBits,
    uint8_t specialCodeBits
)
    : deviceIdBits_(deviceIdBits)
    , connectionIdBits_(connectionIdBits)
    , messageIdBits_(messageIdBits)
    , packageIdBits_(packageIdBits)
    , fragmentIdBits_(fragmentIdBits)
    , fragmentsCountBits_(fragmentsCountBits)
    , priorityBits_(priorityBits)
    , specialCodeBits_(specialCodeBits) {
    validateBits(deviceIdBits_);
    validateBits(connectionIdBits_);
    validateBits(messageIdBits_);
    validateBits(packageIdBits_);
    validateBits(fragmentIdBits_);
    validateBits(fragmentsCountBits_);
    validateBits(priorityBits_);
    validateBits(specialCodeBits_);
}

bool ValidationConfig::validateMessage(const Message& message) const {
    if (message.id < 0 || !fitsInBits(message.id, messageIdBits_)) {
        throw invalid_argument("Message id exceeds allowed bit width");
    }
    if (message.connId < 0 || !fitsInBits(message.connId, connectionIdBits_)) {
        throw invalid_argument("Connection id exceeds allowed bit width");
    }
    if (message.priority < 0 || !fitsInBits(message.priority, priorityBits_)) {
        throw invalid_argument("Message priority exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validatePackage(const Package& package) const {
    if (package.packageId < 0 || !fitsInBits(package.packageId, packageIdBits_)) {
        throw invalid_argument("Package id exceeds allowed bit width");
    }
    if (package.messageId < 0 || !fitsInBits(package.messageId, messageIdBits_)) {
        throw invalid_argument("Package message id exceeds allowed bit width");
    }
    if (package.connId < 0 || !fitsInBits(package.connId, connectionIdBits_)) {
        throw invalid_argument("Package connection id exceeds allowed bit width");
    }
    if (package.fragmentId < 0 || !fitsInBits(package.fragmentId, fragmentIdBits_)) {
        throw invalid_argument("Package fragment id exceeds allowed bit width");
    }
    if (package.fragmentsCount < 0 || !fitsInBits(package.fragmentsCount, fragmentsCountBits_)) {
        throw invalid_argument("Package fragments count exceeds allowed bit width");
    }
    if (package.priority < 0 || !fitsInBits(package.priority, priorityBits_)) {
        throw invalid_argument("Package priority exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validateDeviceId(DeviceId deviceId) const {
    if (deviceId <= 0 || !fitsInBits(deviceId, deviceIdBits_)) {
        throw invalid_argument("Device id exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validateConnectionId(ConnectionId connectionId) const {
    if (connectionId <= 0 || !fitsInBits(connectionId, connectionIdBits_)) {
        throw invalid_argument("Connection id exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validateMessageId(MessageId messageId) const {
    if (messageId <= 0 || !fitsInBits(messageId, messageIdBits_)) {
        throw invalid_argument("Message id exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validatePackageId(PackageId packageId) const {
    if (packageId <= 0 || !fitsInBits(packageId, packageIdBits_)) {
        throw invalid_argument("Package id exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validatePriority(Priority priority) const {
    if (priority < 0 || !fitsInBits(priority, priorityBits_)) {
        throw invalid_argument("Priority exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::validateSpecialCode(int specialCode) const {
    if (specialCode < 0 || !fitsInBits(specialCode, specialCodeBits_)) {
        throw invalid_argument("Special code exceeds allowed bit width");
    }
    return true;
}

bool ValidationConfig::fitsInBits(int value, uint8_t bits) const {
    if (bits == 0 || bits > 32) {
        throw invalid_argument("ValidationConfig: number of bits must be between 1 and 32");
    }
    if (value < 0) {
        return false;
    }
    const uint64_t maxValue = (bits == 32) ? numeric_limits<uint32_t>::max() : ((1ULL << bits) - 1ULL);
    return static_cast<uint64_t>(value) <= maxValue;
}

void ValidationConfig::validateBits(uint8_t bits) const {
    if (bits == 0 || bits > 32) {
        throw invalid_argument("ValidationConfig: number of bits must be between 1 and 32");
    }
}
