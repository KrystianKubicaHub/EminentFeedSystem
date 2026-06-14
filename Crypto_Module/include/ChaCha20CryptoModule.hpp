#pragma once

#include "ICryptoModule.hpp"
#include <array>
#include <mutex>
#include <unordered_map>

/**
 * ChaCha20 stream cipher implementation.
 *
 * - 256-bit key (32 bytes)
 * - 96-bit nonce (12 bytes, randomly generated per message)
 * - Pure software, constant-time, portable to ASIC/ESP32/any platform
 * - Stream cipher: encrypt == decrypt (XOR with keystream)
 *
 * Encrypted format: [keyId: 1B][nonce: 12B][ciphertext: NB]
 */
class ChaCha20CryptoModule : public ICryptoModule {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;
    static constexpr size_t HEADER_SIZE = 1 + NONCE_SIZE;

    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted) override;

    void addKey(uint8_t keyId, const std::vector<uint8_t>& key) override;
    void removeKey(uint8_t keyId) override;
    bool hasKey(uint8_t keyId) const override;

private:
    struct KeyEntry {
        std::array<uint8_t, KEY_SIZE> key;
    };

    mutable std::mutex keysMutex_;
    std::unordered_map<uint8_t, KeyEntry> keys_;

    static void quarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);
    static void chacha20Block(const uint32_t state[16], uint32_t output[16]);
    void chacha20Encrypt(const uint8_t* key, const uint8_t* nonce,
                         uint32_t counter, const uint8_t* input,
                         uint8_t* output, size_t length);
    static std::array<uint8_t, NONCE_SIZE> generateNonce();
};
