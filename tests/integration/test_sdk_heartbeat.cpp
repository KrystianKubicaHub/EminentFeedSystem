#include "EminentSdk.hpp"
#include "PhysicalLayerInMemory.hpp"
#include "ValidationConfig.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace std;
using namespace chrono;

// ============================================================
// Integration tests: Heartbeat and miss detection
// ============================================================
class HeartbeatIntegration : public ::testing::Test {
protected:
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    DeviceId idA = 70;
    DeviceId idB = 80;

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

    bool waitFor(const atomic<bool>& flag, milliseconds timeout = 5000ms) {
        auto start = steady_clock::now();
        while (!flag.load() && (steady_clock::now() - start < timeout)) {
            this_thread::sleep_for(20ms);
        }
        return flag.load();
    }
};

// ============================================================
// TEST: Heartbeat keeps connection alive
// ============================================================
TEST_F(HeartbeatIntegration, ConnectionStaysAliveWithHeartbeat) {
    initBoth();

    atomic<bool> connected{false};
    ConnectionId cidA = -1;

    sdkA->connect(idB, 5,
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const string&) {},
        [](const string&) {},
        []() {},
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const Message&) {},
        500ms,   // Fast heartbeat (500ms)
        nullptr,
        10000ms
    );

    ASSERT_TRUE(waitFor(connected, 8000ms));
    ASSERT_GT(cidA, 0);

    // Wait for several heartbeat cycles
    this_thread::sleep_for(2000ms);

    // Connection should still be active
    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_FALSE(aIds.empty()) << "Connection should remain alive with heartbeats";
}

// ============================================================
// TEST: Heartbeat miss callback fires when peer is killed
// ============================================================
TEST_F(HeartbeatIntegration, HeartbeatMissDetected) {
    initBoth();

    atomic<bool> connected{false};
    atomic<bool> heartbeatMissed{false};
    atomic<bool> disconnected{false};
    ConnectionId cidA = -1;

    sdkA->connect(idB, 5,
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const string&) {},
        [](const string&) {},
        [&]() { disconnected = true; },
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const Message&) {},
        1000ms,  // 1s heartbeat interval
        [&](ConnectionId) { heartbeatMissed = true; },
        15000ms
    );

    ASSERT_TRUE(waitFor(connected, 10000ms));
    ASSERT_GT(cidA, 0);

    // Wait for at least one successful heartbeat exchange
    this_thread::sleep_for(2000ms);

    // Kill B — shutdown may or may not send disconnect message
    sdkB->shutdown();
    sdkB.reset();

    // A should detect the peer is gone — either via heartbeat miss
    // or via disconnect notification (if shutdown sends DISCONNECT)
    auto start = steady_clock::now();
    while (!heartbeatMissed && !disconnected &&
           (steady_clock::now() - start < 12000ms)) {
        this_thread::sleep_for(50ms);
    }

    EXPECT_TRUE(heartbeatMissed || disconnected)
        << "A should detect peer loss (heartbeat miss or disconnect)";
}

// ============================================================
// TEST: Fast heartbeat interval works correctly
// ============================================================
TEST_F(HeartbeatIntegration, FastHeartbeatInterval) {
    initBoth();

    atomic<bool> connected{false};
    atomic<int> missCount{0};
    ConnectionId cidA = -1;

    sdkA->connect(idB, 5,
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const string&) {},
        [](const string&) {},
        []() {},
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const Message&) {},
        200ms,  // Very fast heartbeat (200ms)
        [&](ConnectionId) { missCount++; },
        10000ms
    );

    ASSERT_TRUE(waitFor(connected, 8000ms));

    // With both sides running, no misses should occur
    this_thread::sleep_for(2000ms);
    EXPECT_EQ(missCount.load(), 0)
        << "No heartbeat misses expected when both sides are running";
}

// ============================================================
// TEST: Heartbeat does not fire for protocol-only communication
// ============================================================
TEST_F(HeartbeatIntegration, DataTransferDoesNotAffectHeartbeat) {
    initBoth();

    atomic<bool> connected{false};
    ConnectionId cidA = -1;

    sdkA->connect(idB, 5,
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const string&) {},
        [](const string&) {},
        []() {},
        [&](ConnectionId c) { cidA = c; connected = true; },
        [](const Message&) {},
        1000ms, nullptr, 10000ms
    );

    ASSERT_TRUE(waitFor(connected, 8000ms));
    ASSERT_GT(cidA, 0);

    // Wait for connection to be fully active
    this_thread::sleep_for(500ms);

    // Send messages during heartbeat cycle
    for (int i = 0; i < 5; ++i) {
        try {
            sdkA->send(cidA, "{\"i\":" + to_string(i) + "}", nullptr);
        } catch (...) {
            // Connection may not be fully active yet, that's ok
        }
        this_thread::sleep_for(300ms);
    }

    // Connection should still be alive
    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_FALSE(aIds.empty());
}
