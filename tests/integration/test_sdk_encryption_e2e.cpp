#include "EminentSdk.hpp"
#include "PhysicalLayerInMemory.hpp"
#include "ChaCha20CryptoModule.hpp"
#include "NullCryptoModule.hpp"
#include "ValidationConfig.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono;

// ============================================================
// Encryption end-to-end integration tests
// Both sides have the same pre-shared key, messages should arrive decrypted.
// ============================================================
class EncryptionE2E : public ::testing::Test {
protected:
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    shared_ptr<ChaCha20CryptoModule> cryptoA;
    shared_ptr<ChaCha20CryptoModule> cryptoB;
    DeviceId idA = 10;
    DeviceId idB = 20;
    vector<uint8_t> sharedKey;

    void SetUp() override {
        medium = make_shared<InMemoryMedium>();
        auto plA = make_unique<PhysicalLayerInMemory>(idA, medium);
        auto plB = make_unique<PhysicalLayerInMemory>(idB, medium);
        ValidationConfig vc;
        sdkA = make_unique<EminentSdk>(std::move(plA), vc, LogLevel::NONE);
        sdkB = make_unique<EminentSdk>(std::move(plB), vc, LogLevel::NONE);

        // Create shared key (256-bit)
        sharedKey.resize(32);
        for (int i = 0; i < 32; ++i) sharedKey[i] = static_cast<uint8_t>(i + 0x10);

        // Setup crypto on both sides
        cryptoA = make_shared<ChaCha20CryptoModule>();
        cryptoB = make_shared<ChaCha20CryptoModule>();
        cryptoA->addKey(1, sharedKey);
        cryptoB->addKey(1, sharedKey);

        sdkA->setCryptoModule(cryptoA);
        sdkA->addEncryptionKey(1, sharedKey);
        sdkA->setDefaultEncryptionKey(1);
        sdkA->enableEncryption(true);

        sdkB->setCryptoModule(cryptoB);
        sdkB->addEncryptionKey(1, sharedKey);
        sdkB->setDefaultEncryptionKey(1);
        sdkB->enableEncryption(true);
    }

    void TearDown() override {
        if (sdkA) sdkA->shutdown();
        if (sdkB) sdkB->shutdown();
        this_thread::sleep_for(50ms);
    }

    void initBoth() {
        sdkA->initialize(idA, [](){}, [](const string&){},
                         [](DeviceId, const string&){ return true; }, nullptr);
        sdkB->initialize(idB, [](){}, [](const string&){},
                         [](DeviceId, const string&){ return true; }, nullptr);
    }

    pair<ConnectionId, ConnectionId> connectAtoB() {
        ConnectionId cidA = -1;
        atomic<bool> connected{false};

        sdkA->connect(idB, 5,
            [&](ConnectionId c) { cidA = c; connected = true; },
            [](const string&) {},
            [](const string&) {},
            []() {},
            [&](ConnectionId c) { cidA = c; connected = true; },
            [](const Message&) {},
            2000ms, nullptr, 10000ms
        );

        auto start = steady_clock::now();
        while (!connected && (steady_clock::now() - start < 8000ms)) {
            this_thread::sleep_for(30ms);
        }
        this_thread::sleep_for(200ms);

        auto bIds = sdkB->getActiveConnectionIds();
        ConnectionId cidB = bIds.empty() ? -1 : bIds[0];
        return {cidA, cidB};
    }

    bool waitFor(const atomic<bool>& flag, milliseconds timeout = 5000ms) {
        auto start = steady_clock::now();
        while (!flag.load() && (steady_clock::now() - start < timeout)) {
            this_thread::sleep_for(20ms);
        }
        return flag.load();
    }
};

// ============================================================
// TEST: Encrypted JSON message arrives decrypted on other side
// ============================================================
TEST_F(EncryptionE2E, JsonMessageDecryptedOnReceive) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0) << "A failed to connect";
    ASSERT_GT(cidB, 0) << "B failed to connect";

    atomic<bool> received{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    string original = R"({"text":"Hello encrypted world!","value":42})";
    sdkA->send(cidA, original, MessageFormat::JSON, 5, false, nullptr);

    ASSERT_TRUE(waitFor(received, 5000ms)) << "B did not receive message";
    EXPECT_EQ(receivedPayload, original)
        << "Decrypted payload should match original";
}

// ============================================================
// TEST: Encrypted binary data arrives intact
// ============================================================
TEST_F(EncryptionE2E, BinaryDataDecryptedOnReceive) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> received{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    // Binary data: all byte values 0-255
    vector<uint8_t> binaryData(256);
    for (int i = 0; i < 256; ++i) binaryData[i] = static_cast<uint8_t>(i);

    sdkA->sendBinary(cidA, binaryData, nullptr);

    ASSERT_TRUE(waitFor(received, 5000ms)) << "B did not receive binary data";
    EXPECT_EQ(receivedPayload.size(), 256u);

    // Verify every byte
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(receivedPayload[i]), static_cast<uint8_t>(i))
            << "Byte mismatch at index " << i;
    }
}

// ============================================================
// TEST: Different keys on both sides → message cannot be decrypted correctly
// ============================================================
TEST_F(EncryptionE2E, WrongKeyProducesGarbage) {
    // Give B a different key (same keyId=1 but different data)
    vector<uint8_t> wrongKey(32, 0xFF);
    cryptoB->removeKey(1);
    cryptoB->addKey(1, wrongKey);

    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> received{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    string original = "Secret message that should not be readable";
    sdkA->send(cidA, original, MessageFormat::JSON, 5, false, nullptr);

    // B should receive something (message arrives) but it will be garbled
    waitFor(received, 5000ms);

    if (received) {
        EXPECT_NE(receivedPayload, original)
            << "With wrong key, payload should NOT match original";
    }
    // If not received at all, that's also acceptable (decryption failure can drop message)
}

// ============================================================
// TEST: Encryption disabled → plaintext arrives as-is
// ============================================================
TEST_F(EncryptionE2E, DisabledEncryptionSendsPlaintext) {
    sdkA->enableEncryption(false);
    sdkB->enableEncryption(false);

    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> received{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    string original = "Plaintext message without encryption";
    sdkA->send(cidA, original, MessageFormat::JSON, 5, false, nullptr);

    ASSERT_TRUE(waitFor(received, 5000ms));
    EXPECT_EQ(receivedPayload, original);
}

// ============================================================
// TEST: Per-connection key override
// ============================================================
TEST_F(EncryptionE2E, PerConnectionKeyOverride) {
    // Add second key to both sides
    vector<uint8_t> key2(32, 0xAB);
    cryptoA->addKey(2, key2);
    cryptoB->addKey(2, key2);
    sdkA->addEncryptionKey(2, key2);
    sdkB->addEncryptionKey(2, key2);

    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    // Override connection to use key 2
    sdkA->setConnectionEncryptionKey(cidA, 2);

    atomic<bool> received{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    string original = "Message encrypted with key 2";
    sdkA->send(cidA, original, MessageFormat::JSON, 5, false, nullptr);

    ASSERT_TRUE(waitFor(received, 5000ms));
    EXPECT_EQ(receivedPayload, original);
}
