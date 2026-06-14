#include <gtest/gtest.h>
#include "ChaCha20CryptoModule.hpp"
#include "NullCryptoModule.hpp"
#include <string>
#include <vector>

// ============================================================
// NullCryptoModule Tests
// ============================================================

TEST(NullCrypto, EncryptDecryptRoundtrip) {
    NullCryptoModule crypto;
    crypto.addKey(1, {});

    std::string original = "Hello, World!";
    std::vector<uint8_t> plaintext(original.begin(), original.end());

    auto encrypted = crypto.encrypt(plaintext, 1);
    EXPECT_EQ(encrypted[0], 1);
    EXPECT_EQ(encrypted.size(), plaintext.size() + 1);

    auto decrypted = crypto.decrypt(encrypted);
    std::string result(decrypted.begin(), decrypted.end());
    EXPECT_EQ(result, original);
}

TEST(NullCrypto, HasKey) {
    NullCryptoModule crypto;
    EXPECT_FALSE(crypto.hasKey(5));
    crypto.addKey(5, {});
    EXPECT_TRUE(crypto.hasKey(5));
    crypto.removeKey(5);
    EXPECT_FALSE(crypto.hasKey(5));
}

TEST(NullCrypto, EmptyPayload) {
    NullCryptoModule crypto;
    crypto.addKey(0, {});

    std::vector<uint8_t> empty;
    auto encrypted = crypto.encrypt(empty, 0);
    EXPECT_EQ(encrypted.size(), 1u);

    auto decrypted = crypto.decrypt(encrypted);
    EXPECT_TRUE(decrypted.empty());
}

TEST(NullCrypto, DecryptEmptyThrows) {
    NullCryptoModule crypto;
    std::vector<uint8_t> empty;
    EXPECT_THROW(crypto.decrypt(empty), std::runtime_error);
}

// ============================================================
// ChaCha20CryptoModule Tests
// ============================================================

class ChaCha20Test : public ::testing::Test {
protected:
    ChaCha20CryptoModule crypto;
    std::vector<uint8_t> testKey;

    void SetUp() override {
        testKey.resize(32);
        for (int i = 0; i < 32; ++i) testKey[i] = static_cast<uint8_t>(i + 1);
        crypto.addKey(1, testKey);
    }
};

TEST_F(ChaCha20Test, EncryptDecryptRoundtrip) {
    std::string original = "Hello, encrypted world! This is a test message.";
    std::vector<uint8_t> plaintext(original.begin(), original.end());

    auto encrypted = crypto.encrypt(plaintext, 1);
    EXPECT_EQ(encrypted.size(), 1 + 12 + plaintext.size());
    EXPECT_EQ(encrypted[0], 1);

    std::vector<uint8_t> ciphertextOnly(encrypted.begin() + 13, encrypted.end());
    EXPECT_NE(ciphertextOnly, plaintext);

    auto decrypted = crypto.decrypt(encrypted);
    std::string result(decrypted.begin(), decrypted.end());
    EXPECT_EQ(result, original);
}

TEST_F(ChaCha20Test, DifferentNonces) {
    std::string msg = "Same message twice";
    std::vector<uint8_t> plaintext(msg.begin(), msg.end());

    auto enc1 = crypto.encrypt(plaintext, 1);
    auto enc2 = crypto.encrypt(plaintext, 1);
    EXPECT_NE(enc1, enc2);

    auto dec1 = crypto.decrypt(enc1);
    auto dec2 = crypto.decrypt(enc2);
    EXPECT_EQ(dec1, dec2);
}

TEST_F(ChaCha20Test, WrongKeyProducesGarbage) {
    std::string msg = "Secret data";
    std::vector<uint8_t> plaintext(msg.begin(), msg.end());
    auto encrypted = crypto.encrypt(plaintext, 1);

    ChaCha20CryptoModule other;
    std::vector<uint8_t> wrongKey(32, 0xFF);
    other.addKey(1, wrongKey);

    auto decrypted = other.decrypt(encrypted);
    std::string result(decrypted.begin(), decrypted.end());
    EXPECT_NE(result, msg);
}

TEST_F(ChaCha20Test, UnknownKeyIdThrows) {
    std::vector<uint8_t> pt = {1, 2, 3};
    EXPECT_THROW(crypto.encrypt(pt, 99), std::runtime_error);
}

TEST_F(ChaCha20Test, DecryptUnknownKeyThrows) {
    std::vector<uint8_t> fake(20, 0);
    fake[0] = 77;
    EXPECT_THROW(crypto.decrypt(fake), std::runtime_error);
}

TEST_F(ChaCha20Test, DecryptTooShortThrows) {
    std::vector<uint8_t> tooShort = {1, 2, 3};
    EXPECT_THROW(crypto.decrypt(tooShort), std::runtime_error);
}

TEST_F(ChaCha20Test, InvalidKeySizeThrows) {
    std::vector<uint8_t> shortKey = {1, 2, 3, 4};
    EXPECT_THROW(crypto.addKey(2, shortKey), std::runtime_error);
}

TEST_F(ChaCha20Test, EmptyPayload) {
    std::vector<uint8_t> empty;
    auto encrypted = crypto.encrypt(empty, 1);
    EXPECT_EQ(encrypted.size(), 13u);
    auto decrypted = crypto.decrypt(encrypted);
    EXPECT_TRUE(decrypted.empty());
}

TEST_F(ChaCha20Test, LargePayload) {
    std::vector<uint8_t> large(10240);
    for (size_t i = 0; i < large.size(); ++i) large[i] = static_cast<uint8_t>(i & 0xFF);
    auto encrypted = crypto.encrypt(large, 1);
    auto decrypted = crypto.decrypt(encrypted);
    EXPECT_EQ(decrypted, large);
}

TEST_F(ChaCha20Test, MultipleKeys) {
    std::vector<uint8_t> key2(32, 0xAA);
    std::vector<uint8_t> key3(32, 0xBB);
    crypto.addKey(2, key2);
    crypto.addKey(3, key3);

    std::string msg = "Multi-key test";
    std::vector<uint8_t> pt(msg.begin(), msg.end());

    auto e1 = crypto.encrypt(pt, 1);
    auto e2 = crypto.encrypt(pt, 2);
    auto e3 = crypto.encrypt(pt, 3);

    auto d1 = crypto.decrypt(e1);
    auto d2 = crypto.decrypt(e2);
    auto d3 = crypto.decrypt(e3);

    EXPECT_EQ(std::string(d1.begin(), d1.end()), msg);
    EXPECT_EQ(std::string(d2.begin(), d2.end()), msg);
    EXPECT_EQ(std::string(d3.begin(), d3.end()), msg);
}

TEST_F(ChaCha20Test, RemoveKey) {
    EXPECT_TRUE(crypto.hasKey(1));
    crypto.removeKey(1);
    EXPECT_FALSE(crypto.hasKey(1));
    std::vector<uint8_t> pt = {1, 2, 3};
    EXPECT_THROW(crypto.encrypt(pt, 1), std::runtime_error);
}

TEST_F(ChaCha20Test, BinaryDataIntegrity) {
    std::vector<uint8_t> allBytes(256);
    for (int i = 0; i < 256; ++i) allBytes[i] = static_cast<uint8_t>(i);
    auto encrypted = crypto.encrypt(allBytes, 1);
    auto decrypted = crypto.decrypt(encrypted);
    EXPECT_EQ(decrypted, allBytes);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
