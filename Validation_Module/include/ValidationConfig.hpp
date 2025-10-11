#pragma once

#include <cstdint>
#include <commonTypes.hpp>

using namespace std;

class ValidationConfig {
public:
    ValidationConfig();

    bool validateMessage(const Message& message) const;
    bool validatePackage(const Package& package) const;

private:
    bool fitsInBits(int value, uint8_t bits) const;

    const uint8_t deviceIdBits_;
    const uint8_t connectionIdBits_;
    const uint8_t messageIdBits_;
    const uint8_t packageIdBits_;
    const uint8_t fragmentIdBits_;
    const uint8_t fragmentsCountBits_;
    const uint8_t priorityBits_;
    const uint8_t specialCodeBits_;
};
