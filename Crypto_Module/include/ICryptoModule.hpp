#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Abstract interface for payload encryption/decryption.
 * Implementations can be swapped (e.g. ChaCha20, AES-128, hardware ASIC crypto).
 */
class ICryptoModule {
public:
    virtual ~ICryptoModule() = default;

    /**
     * Encrypt a plaintext payload.
     * @param plaintext Raw payload bytes
     * @param keyId Which pre-shared key to use
     * @return Encrypted blob: [keyId(1B)][nonce(12B)][ciphertext(NB)]
     */
    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) = 0;

    /**
     * Decrypt an encrypted payload.
     * @param encrypted Full encrypted blob (as produced by encrypt())
     * @return Decrypted plaintext bytes
     */
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted) = 0;

    /**
     * Add a pre-shared key.
     * @param keyId Identifier (0-255)
     * @param key Key bytes (length depends on implementation, e.g. 32 for ChaCha20)
     */
    virtual void addKey(uint8_t keyId, const std::vector<uint8_t>& key) = 0;

    /** Remove a key. */
    virtual void removeKey(uint8_t keyId) = 0;

    /** Check if a key exists. */
    virtual bool hasKey(uint8_t keyId) const = 0;
};
