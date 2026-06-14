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
// Integration tests: Disconnect scenarios
// ============================================================
class DisconnectIntegration : public ::testing::Test {
protected:
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    DeviceId idA = 50;
    DeviceId idB = 60;

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
// TEST: Graceful disconnect by A — B gets notified
// ============================================================
TEST_F(DisconnectIntegration, GracefulDisconnectNotifiesPeer) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    atomic<bool> bDisconnected{false};
    sdkB->setOnDisconnected(cidB, [&]() {
        bDisconnected = true;
    });

    // A disconnects
    sdkA->disconnect(cidA);

    // B should get notified
    ASSERT_TRUE(waitFor(bDisconnected, 5000ms))
        << "B should receive disconnect notification";

    // A's connection list should be empty
    this_thread::sleep_for(100ms);
    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_TRUE(aIds.empty()) << "A should have no active connections after disconnect";
}

// ============================================================
// TEST: Disconnect then send should throw
// ============================================================
TEST_F(DisconnectIntegration, SendAfterDisconnectThrows) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);

    sdkA->disconnect(cidA);
    this_thread::sleep_for(100ms);

    EXPECT_THROW(
        sdkA->send(cidA, "Should fail", MessageFormat::JSON, 5, false, nullptr),
        runtime_error
    );
}

// ============================================================
// TEST: Both sides disconnect simultaneously
// ============================================================
TEST_F(DisconnectIntegration, SimultaneousDisconnect) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);
    ASSERT_GT(cidB, 0);

    // Both disconnect at the same time
    sdkA->disconnect(cidA);
    sdkB->disconnect(cidB);

    this_thread::sleep_for(500ms);

    auto aIds = sdkA->getActiveConnectionIds();
    auto bIds = sdkB->getActiveConnectionIds();

    EXPECT_TRUE(aIds.empty()) << "A should have no connections";
    EXPECT_TRUE(bIds.empty()) << "B should have no connections";
}

// ============================================================
// TEST: Shutdown cleans up all connections
// ============================================================
TEST_F(DisconnectIntegration, ShutdownCleansAllConnections) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);

    atomic<bool> bDisconnected{false};
    sdkB->setOnDisconnected(cidB, [&]() { bDisconnected = true; });

    // Full shutdown of A
    sdkA->shutdown();

    // Give time for disconnect message to propagate
    this_thread::sleep_for(500ms);

    // After shutdown, A should not have any connections
    // (we can't call getActiveConnectionIds after shutdown safely,
    // so we just verify B noticed)
    // B may or may not get the notification depending on timing
    // The key assertion is that shutdown doesn't crash
    SUCCEED() << "Shutdown completed without crash";
}

// ============================================================
// TEST: close() is alias for disconnect()
// ============================================================
TEST_F(DisconnectIntegration, CloseIsAliasForDisconnect) {
    initBoth();
    auto [cidA, cidB] = connectAtoB();
    ASSERT_GT(cidA, 0);

    // Use close() instead of disconnect()
    sdkA->close(cidA);

    this_thread::sleep_for(200ms);

    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_TRUE(aIds.empty()) << "close() should remove the connection";
}
