#include "EminentSdk.hpp"
#include "PhysicalLayerInMemory.hpp"
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
// Integration tests: Retransmission (reliable delivery)
// ============================================================
class RetransmissionIntegration : public ::testing::Test {
protected:
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    DeviceId idA = 30;
    DeviceId idB = 40;

    void SetUp() override {
        medium = make_shared<InMemoryMedium>();
        auto plA = make_unique<PhysicalLayerInMemory>(idA, medium);
        auto plB = make_unique<PhysicalLayerInMemory>(idB, medium);
        ValidationConfig vc;
        sdkA = make_unique<EminentSdk>(std::move(plA), vc, LogLevel::NONE);
        sdkB = make_unique<EminentSdk>(std::move(plB), vc, LogLevel::NONE);
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
// TEST: Message with requireAck=true gets delivered
// ============================================================
TEST_F(RetransmissionIntegration, AckedMessageDelivered) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> received{false};
    atomic<bool> delivered{false};
    string receivedPayload;

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    sdkA->send(cidA, "Reliable message", MessageFormat::JSON, 5, true,
               [&]() { delivered = true; });

    ASSERT_TRUE(waitFor(received, 5000ms)) << "B should receive the message";
    EXPECT_EQ(receivedPayload, "Reliable message");
}

// ============================================================
// TEST: Message with requireAck=false also gets delivered (best effort)
// ============================================================
TEST_F(RetransmissionIntegration, BestEffortMessageDelivered) {
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

    sdkA->send(cidA, "Best effort msg", MessageFormat::JSON, 5, false, nullptr);

    ASSERT_TRUE(waitFor(received, 5000ms));
    EXPECT_EQ(receivedPayload, "Best effort msg");
}

// ============================================================
// TEST: Multiple messages arrive in order
// ============================================================
TEST_F(RetransmissionIntegration, MessagesArriveInOrder) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    vector<string> receivedMessages;
    atomic<int> count{0};

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        receivedMessages.push_back(msg.payload);
        count++;
    });

    int numMessages = 10;
    for (int i = 0; i < numMessages; ++i) {
        sdkA->send(cidA, "msg_" + to_string(i), MessageFormat::JSON, 5, true, nullptr);
    }

    // Wait for all messages
    auto start = steady_clock::now();
    while (count.load() < numMessages && (steady_clock::now() - start < 10000ms)) {
        this_thread::sleep_for(50ms);
    }

    EXPECT_EQ(count.load(), numMessages) << "All messages should arrive";

    // Check ordering
    for (int i = 0; i < min((int)receivedMessages.size(), numMessages); ++i) {
        EXPECT_EQ(receivedMessages[i], "msg_" + to_string(i))
            << "Message order mismatch at index " << i;
    }
}

// ============================================================
// TEST: Retransmission config can be changed
// ============================================================
TEST_F(RetransmissionIntegration, RetransmissionConfigUpdate) {
    sdkA->setRetransmissionConfig(5, 200ms);
    EXPECT_EQ(sdkA->getMaxRetransmitAttempts(), 5);
    EXPECT_EQ(sdkA->getRetransmitInterval(), 200ms);

    // Invalid config should be rejected
    sdkA->setRetransmissionConfig(0, 100ms); // 0 attempts → ignored
    EXPECT_EQ(sdkA->getMaxRetransmitAttempts(), 5); // unchanged

    sdkA->setRetransmissionConfig(3, 5ms); // 5ms < 10ms → ignored
    EXPECT_EQ(sdkA->getRetransmitInterval(), 200ms); // unchanged
}

// ============================================================
// TEST: Large payload (multiple fragments) delivered intact
// ============================================================
TEST_F(RetransmissionIntegration, LargePayloadDeliveredIntact) {
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

    // Create a large message (4KB — likely requires fragmentation)
    string largePayload(4096, 'X');
    sdkA->send(cidA, largePayload, MessageFormat::JSON, 5, true, nullptr);

    ASSERT_TRUE(waitFor(received, 10000ms)) << "Large payload should arrive";
    EXPECT_EQ(receivedPayload.size(), 4096u);
    EXPECT_EQ(receivedPayload, largePayload);
}

// ============================================================
// TEST: Bidirectional communication
// ============================================================
TEST_F(RetransmissionIntegration, BidirectionalCommunication) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> aReceived{false};
    atomic<bool> bReceived{false};
    string aPayload, bPayload;

    sdkA->setOnMessageHandler(cidA, [&](const Message& msg) {
        aPayload = msg.payload;
        aReceived = true;
    });

    sdkB->setOnMessageHandler(cidB, [&](const Message& msg) {
        bPayload = msg.payload;
        bReceived = true;
    });

    // A sends to B
    sdkA->send(cidA, "Hello B from A", MessageFormat::JSON, 5, false, nullptr);
    // B sends to A
    sdkB->send(cidB, "Hello A from B", MessageFormat::JSON, 5, false, nullptr);

    ASSERT_TRUE(waitFor(bReceived, 5000ms)) << "B should receive from A";
    ASSERT_TRUE(waitFor(aReceived, 5000ms)) << "A should receive from B";

    EXPECT_EQ(bPayload, "Hello B from A");
    EXPECT_EQ(aPayload, "Hello A from B");
}
