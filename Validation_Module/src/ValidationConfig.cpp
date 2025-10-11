#include "ValidationConfig.hpp"

#include <limits>
#include <stdexcept>
#include <string>

using namespace std;

namespace {
constexpr uint8_t DEVICE_ID_BITS = 16;
constexpr uint8_t CONNECTION_ID_BITS = 16;
constexpr uint8_t MESSAGE_ID_BITS = 24;
constexpr uint8_t PACKAGE_ID_BITS = 24;
constexpr uint8_t FRAGMENT_ID_BITS = 8;
constexpr uint8_t FRAGMENTS_COUNT_BITS = 8;
constexpr uint8_t PRIORITY_BITS = 4;
constexpr uint8_t SPECIAL_CODE_BITS = 16;
}

ValidationConfig::ValidationConfig()
    : deviceIdBits_(DEVICE_ID_BITS)
    , connectionIdBits_(CONNECTION_ID_BITS)
    , messageIdBits_(MESSAGE_ID_BITS)
    , packageIdBits_(PACKAGE_ID_BITS)
    , fragmentIdBits_(FRAGMENT_ID_BITS)
    , fragmentsCountBits_(FRAGMENTS_COUNT_BITS)
    , priorityBits_(PRIORITY_BITS)
    , specialCodeBits_(SPECIAL_CODE_BITS) {}

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
