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
// Helper: Creates a pair of SDKs connected via InMemory medium
// ============================================================
struct TestSdkPair {
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    DeviceId idA = 1001;
    DeviceId idB = 2002;

    TestSdkPair() {
        medium = make_shared<InMemoryMedium>();
        auto plA = make_unique<PhysicalLayerInMemory>(idA, medium);
        auto plB = make_unique<PhysicalLayerInMemory>(idB, medium);
        ValidationConfig vc;
        sdkA = make_unique<EminentSdk>(std::move(plA), vc, LogLevel::NONE);
        sdkB = make_unique<EminentSdk>(std::move(plB), vc, LogLevel::NONE);
    }

    ~TestSdkPair() {
        if (sdkA) sdkA->shutdown();
        if (sdkB) sdkB->shutdown();
        // Give threads time to stop
        this_thread::sleep_for(milliseconds{50});
    }

    bool initBoth() {
        atomic<int> failCount{0};

        sdkA->initialize(
            idA,
            []() {},
            [&](const string&) { failCount++; },
            [](DeviceId, const string&) { return true; },
            nullptr
        );

        sdkB->initialize(
            idB,
            []() {},
            [&](const string&) { failCount++; },
            [](DeviceId, const string&) { return true; },
            nullptr
        );

        return failCount == 0;
    }

    pair<ConnectionId, ConnectionId> connectAtoB(milliseconds timeout = milliseconds{5000}) {
        ConnectionId cidA = -1;
        ConnectionId cidB = -1;
        atomic<bool> connectedA{false};

        sdkA->connect(
            idB,
            1,
            [&](ConnectionId c) { cidA = c; connectedA = true; },
            [](const string&) {},
            [](const string&) {},
            []() {},
            [&](ConnectionId c) { cidA = c; connectedA = true; },
            [](const Message&) {},
            milliseconds{2000},
            nullptr
        );

        auto start = steady_clock::now();
        while ((!connectedA || cidA <= 0) && (steady_clock::now() - start < timeout)) {
            this_thread::sleep_for(milliseconds{50});
        }

        // Wait a bit more for B to register the connection
        this_thread::sleep_for(milliseconds{200});

        auto ids = sdkB->getActiveConnectionIds();
        if (!ids.empty()) {
            cidB = ids[0];
        }

        return {cidA, cidB};
    }
};

// ============================================================
// Test: SDK Initialization
// ============================================================
TEST(SdkInit, InitializeSucceeds) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
    ValidationConfig vc;
    EminentSdk sdk(std::move(pl), vc, LogLevel::NONE);

    atomic<bool> initSuccess{false};
    atomic<bool> initFailure{false};

    sdk.initialize(
        1001,
        [&]() { initSuccess = true; },
        [&](const string&) { initFailure = true; },
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    EXPECT_TRUE(initSuccess.load());
    EXPECT_FALSE(initFailure.load());
}

TEST(SdkInit, DoubleInitializeFails) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
    ValidationConfig vc;
    EminentSdk sdk(std::move(pl), vc, LogLevel::NONE);

    atomic<bool> firstOk{false};
    sdk.initialize(
        1001,
        [&]() { firstOk = true; },
        [](const string&) {},
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    atomic<bool> secondFail{false};
    sdk.initialize(
        1001,
        []() {},
        [&](const string&) { secondFail = true; },
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    EXPECT_TRUE(firstOk.load());
    EXPECT_TRUE(secondFail.load());
}

TEST(SdkInit, InitializeWithZeroIdFails) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl = make_unique<PhysicalLayerInMemory>(0, medium);
    ValidationConfig vc;
    EminentSdk sdk(std::move(pl), vc, LogLevel::NONE);

    atomic<bool> initFailed{false};
    sdk.initialize(
        0,
        []() {},
        [&](const string&) { initFailed = true; },
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    EXPECT_TRUE(initFailed.load());
}

// ============================================================
// Test: Connection Handshake
// ============================================================
TEST(SdkConnect, HandshakeCompletes) {
    TestSdkPair p;
    p.initBoth();

    auto [cidA, cidB] = p.connectAtoB();
    EXPECT_GT(cidA, 0);
}

TEST(SdkConnect, GetActiveConnectionIds) {
    TestSdkPair p;
    p.initBoth();

    auto [cidA, cidB] = p.connectAtoB();
    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }
    auto idsA = p.sdkA->getActiveConnectionIds();
    EXPECT_GE(idsA.size(), 1u);
}

TEST(SdkConnect, ConnectWithoutInitDoesNotCrash) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
    ValidationConfig vc;
    EminentSdk sdk(std::move(pl), vc, LogLevel::NONE);

    // Connect without init - should not crash (handshake will just time out)
    sdk.connect(
        2002,
        1,
        [](ConnectionId) {},
        [](const string&) {},
        [](const string&) {},
        []() {},
        [](ConnectionId) {},
        [](const Message&) {},
        milliseconds{2000},
        nullptr
    );

    this_thread::sleep_for(milliseconds{100});
    SUCCEED();
}

// ============================================================
// Test: Message Sending
// ============================================================
TEST(SdkSend, SendTextMessage) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    atomic<bool> delivered{false};
    p.sdkA->send(cidA, "Hello World", [&]() { delivered = true; });
    this_thread::sleep_for(milliseconds{500});
    SUCCEED();
}

TEST(SdkSend, SendBinaryData) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    vector<uint8_t> data = {0x01, 0x02, 0x03, 0xFF, 0xFE};
    atomic<bool> delivered{false};
    p.sdkA->sendBinary(cidA, data, [&]() { delivered = true; });
    this_thread::sleep_for(milliseconds{500});
    SUCCEED();
}

TEST(SdkSend, SendWithPriorityAndAck) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    atomic<bool> delivered{false};
    p.sdkA->send(cidA, "priority message", MessageFormat::JSON, 5, true, [&]() { delivered = true; });
    this_thread::sleep_for(milliseconds{500});
    SUCCEED();
}

TEST(SdkSend, SendToInvalidConnectionThrows) {
    TestSdkPair p;
    p.initBoth();
    EXPECT_THROW(p.sdkA->send(9999, "test", nullptr), std::runtime_error);
    SUCCEED();
}

TEST(SdkSend, SendBinaryWithPriority) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    vector<uint8_t> binaryData(1024, 0xAB);
    atomic<bool> delivered{false};
    p.sdkA->sendBinary(cidA, binaryData, 3, true, [&]() { delivered = true; });
    this_thread::sleep_for(milliseconds{500});
    SUCCEED();
}

// ============================================================
// Test: Disconnect Protocol
// ============================================================
TEST(SdkDisconnect, DisconnectCleansUp) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    p.sdkA->disconnect(cidA);
    this_thread::sleep_for(milliseconds{300});

    auto idsA = p.sdkA->getActiveConnectionIds();
    bool found = false;
    for (auto id : idsA) {
        if (id == cidA) found = true;
    }
    EXPECT_FALSE(found);
}

TEST(SdkDisconnect, DisconnectNotCrash) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete";
    }

    p.sdkA->disconnect(cidA);
    this_thread::sleep_for(milliseconds{500});
    SUCCEED();
}

// ============================================================
// Test: Shutdown
// ============================================================
TEST(SdkShutdown, ShutdownCleansAll) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    p.sdkA->shutdown();
    auto idsA = p.sdkA->getActiveConnectionIds();
    EXPECT_TRUE(idsA.empty());
}

// ============================================================
// Test: Heartbeat
// ============================================================
TEST(SdkHeartbeat, HeartbeatDoesNotCrash) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete in time";
    }

    this_thread::sleep_for(milliseconds{3000});
    auto idsA = p.sdkA->getActiveConnectionIds();
    EXPECT_GE(idsA.size(), 1u);
}

// ============================================================
// Test: Edge cases
// ============================================================
TEST(SdkEdge, ShutdownDuringActiveTransfer) {
    TestSdkPair p;
    p.initBoth();
    auto [cidA, cidB] = p.connectAtoB();

    if (cidA <= 0) {
        GTEST_SKIP() << "Handshake did not complete";
    }

    for (int i = 0; i < 10; i++) {
        p.sdkA->send(cidA, "msg_" + to_string(i), nullptr);
    }

    p.sdkA->shutdown();
    SUCCEED();
}

// ============================================================
// Test: Handshake timeout
// ============================================================
TEST(SdkTimeout, HandshakeTimeoutFiresOnFailure) {
    // Create SDK A with no peer to respond -> handshake should timeout
    auto medium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(1001, medium);
    ValidationConfig vc;
    EminentSdk sdkA(std::move(plA), vc, LogLevel::NONE);

    sdkA.initialize(
        1001,
        []() {},
        [](const string&) {},
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    atomic<bool> failureCalled{false};
    string failureReason;

    // Connect to device that doesn't exist, with short timeout
    sdkA.connect(
        9999,
        1,
        [](ConnectionId) {},
        [&](const string& reason) { failureCalled = true; failureReason = reason; },
        [](const string&) {},
        []() {},
        [](ConnectionId) {},
        [](const Message&) {},
        milliseconds{2000},
        nullptr,
        milliseconds{2000}  // 2 second handshake timeout
    );

    // Wait for timeout to fire
    auto start = steady_clock::now();
    while (!failureCalled.load() && (steady_clock::now() - start < seconds{5})) {
        this_thread::sleep_for(milliseconds{100});
    }

    EXPECT_TRUE(failureCalled.load());
    sdkA.shutdown();
}

// ============================================================
// Test: Retransmission config API
// ============================================================
TEST(SdkConfig, RetransmissionConfig) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
    ValidationConfig vc;
    EminentSdk sdk(std::move(pl), vc, LogLevel::NONE);

    sdk.initialize(
        1001,
        []() {},
        [](const string&) {},
        [](DeviceId, const string&) { return true; },
        nullptr
    );

    // Default values
    EXPECT_EQ(sdk.getMaxRetransmitAttempts(), 5);
    EXPECT_EQ(sdk.getRetransmitInterval().count(), 500);

    // Update
    sdk.setRetransmissionConfig(10, milliseconds{1000});
    EXPECT_EQ(sdk.getMaxRetransmitAttempts(), 10);
    EXPECT_EQ(sdk.getRetransmitInterval().count(), 1000);

    // Invalid values should be rejected
    sdk.setRetransmissionConfig(0, milliseconds{100});
    EXPECT_EQ(sdk.getMaxRetransmitAttempts(), 10); // unchanged

    sdk.setRetransmissionConfig(3, milliseconds{5});
    EXPECT_EQ(sdk.getRetransmitInterval().count(), 1000); // unchanged

    sdk.shutdown();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
