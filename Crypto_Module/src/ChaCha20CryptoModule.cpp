#include "ChaCha20CryptoModule.hpp"
#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>

static inline uint32_t rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

void ChaCha20CryptoModule::quarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

void ChaCha20CryptoModule::chacha20Block(const uint32_t state[16], uint32_t output[16]) {
    for (int i = 0; i < 16; ++i) output[i] = state[i];

    for (int i = 0; i < 10; ++i) {
        quarterRound(output[0], output[4], output[8],  output[12]);
        quarterRound(output[1], output[5], output[9],  output[13]);
        quarterRound(output[2], output[6], output[10], output[14]);
        quarterRound(output[3], output[7], output[11], output[15]);
        quarterRound(output[0], output[5], output[10], output[15]);
        quarterRound(output[1], output[6], output[11], output[12]);
        quarterRound(output[2], output[7], output[8],  output[13]);
        quarterRound(output[3], output[4], output[9],  output[14]);
    }

    for (int i = 0; i < 16; ++i) output[i] += state[i];
}

void ChaCha20CryptoModule::chacha20Encrypt(
    const uint8_t* key, const uint8_t* nonce,
    uint32_t counter, const uint8_t* input,
    uint8_t* output, size_t length
) {
    uint32_t state[16];
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;
    std::memcpy(&state[4], key, 32);
    state[12] = counter;
    std::memcpy(&state[13], nonce, 12);

    size_t offset = 0;
    while (offset < length) {
        uint32_t keystream[16];
        chacha20Block(state, keystream);

        size_t blockBytes = std::min(length - offset, size_t(64));
        const uint8_t* ks = reinterpret_cast<const uint8_t*>(keystream);
        for (size_t i = 0; i < blockBytes; ++i) {
            output[offset + i] = input[offset + i] ^ ks[i];
        }

        offset += blockBytes;
        state[12]++;
    }
}

std::array<uint8_t, ChaCha20CryptoModule::NONCE_SIZE> ChaCha20CryptoModule::generateNonce() {
    std::array<uint8_t, NONCE_SIZE> nonce;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist(0, 255);
    for (auto& b : nonce) {
        b = static_cast<uint8_t>(dist(gen));
    }
    return nonce;
}

void ChaCha20CryptoModule::addKey(uint8_t keyId, const std::vector<uint8_t>& key) {
    if (key.size() != KEY_SIZE) {
        throw std::runtime_error(
            "ChaCha20CryptoModule::addKey: key must be exactly 32 bytes, got " +
            std::to_string(key.size()));
    }
    std::lock_guard<std::mutex> lock(keysMutex_);
    KeyEntry entry;
    std::memcpy(entry.key.data(), key.data(), KEY_SIZE);
    keys_[keyId] = entry;
}

void ChaCha20CryptoModule::removeKey(uint8_t keyId) {
    std::lock_guard<std::mutex> lock(keysMutex_);
    keys_.erase(keyId);
}

bool ChaCha20CryptoModule::hasKey(uint8_t keyId) const {
    std::lock_guard<std::mutex> lock(keysMutex_);
    return keys_.count(keyId) > 0;
}

std::vector<uint8_t> ChaCha20CryptoModule::encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) {
    std::lock_guard<std::mutex> lock(keysMutex_);
    auto it = keys_.find(keyId);
    if (it == keys_.end()) {
        throw std::runtime_error("ChaCha20CryptoModule::encrypt: unknown keyId=" + std::to_string(keyId));
    }

    auto nonce = generateNonce();
    std::vector<uint8_t> result;
    result.resize(HEADER_SIZE + plaintext.size());
    result[0] = keyId;
    std::memcpy(result.data() + 1, nonce.data(), NONCE_SIZE);

    if (!plaintext.empty()) {
        chacha20Encrypt(it->second.key.data(), nonce.data(), 1,
                        plaintext.data(), result.data() + HEADER_SIZE, plaintext.size());
    }

    return result;
}

std::vector<uint8_t> ChaCha20CryptoModule::decrypt(const std::vector<uint8_t>& encrypted) {
    if (encrypted.size() < HEADER_SIZE) {
        throw std::runtime_error("ChaCha20CryptoModule::decrypt: input too short (" +
            std::to_string(encrypted.size()) + " bytes)");
    }

    uint8_t keyId = encrypted[0];
    std::lock_guard<std::mutex> lock(keysMutex_);
    auto it = keys_.find(keyId);
    if (it == keys_.end()) {
        throw std::runtime_error("ChaCha20CryptoModule::decrypt: unknown keyId=" + std::to_string(keyId));
    }

    const uint8_t* nonce = encrypted.data() + 1;
    const uint8_t* ciphertext = encrypted.data() + HEADER_SIZE;
    size_t ciphertextLen = encrypted.size() - HEADER_SIZE;

    std::vector<uint8_t> plaintext(ciphertextLen);
    if (ciphertextLen > 0) {
        chacha20Encrypt(it->second.key.data(), nonce, 1,
                        ciphertext, plaintext.data(), ciphertextLen);
    }

    return plaintext;
}
