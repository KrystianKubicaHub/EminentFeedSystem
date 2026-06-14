#pragma once

#include "ICryptoModule.hpp"
#include <stdexcept>
#include <unordered_map>

/**
 * No-op crypto module — passes data through without encryption.
 * Used for testing or when encryption is not needed.
 */
class NullCryptoModule : public ICryptoModule {
public:
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) override {
        std::vector<uint8_t> result;
        result.reserve(1 + plaintext.size());
        result.push_back(keyId);
        result.insert(result.end(), plaintext.begin(), plaintext.end());
        return result;
    }

    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted) override {
        if (encrypted.empty()) {
            throw std::runtime_error("NullCryptoModule::decrypt: empty input");
        }
        return std::vector<uint8_t>(encrypted.begin() + 1, encrypted.end());
    }

    void addKey(uint8_t keyId, const std::vector<uint8_t>& /*key*/) override {
        keys_[keyId] = true;
    }

    void removeKey(uint8_t keyId) override {
        keys_.erase(keyId);
    }

    bool hasKey(uint8_t keyId) const override {
        return keys_.count(keyId) > 0;
    }

private:
    std::unordered_map<uint8_t, bool> keys_;
};
