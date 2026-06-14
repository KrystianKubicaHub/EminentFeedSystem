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
// Integration test fixture: Two SDK instances connected via InMemory
// ============================================================
class HandshakeIntegration : public ::testing::Test {
protected:
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdkA;
    unique_ptr<EminentSdk> sdkB;
    DeviceId idA = 100;
    DeviceId idB = 200;

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
// TEST: Successful 3-way handshake A → B
// ============================================================
TEST_F(HandshakeIntegration, SuccessfulConnection) {
    initBoth();

    atomic<bool> aConnected{false};
    ConnectionId cidA = -1;

    sdkA->connect(
        idB, 5,
        [&](ConnectionId c) { cidA = c; aConnected = true; },
        [](const string&) { FAIL() << "Connection failed"; },
        [](const string&) {},
        []() {},
        [&](ConnectionId c) { cidA = c; aConnected = true; },
        [](const Message&) {},
        2000ms, nullptr, 10000ms
    );

    ASSERT_TRUE(waitFor(aConnected, 8000ms)) << "A did not connect in time";
    EXPECT_GT(cidA, 0);

    // B should see the connection
    this_thread::sleep_for(200ms);
    auto bIds = sdkB->getActiveConnectionIds();
    EXPECT_FALSE(bIds.empty()) << "B has no active connections";
}

// ============================================================
// TEST: Connection rejected by B
// ============================================================
TEST_F(HandshakeIntegration, ConnectionRejected) {
    medium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(idA, medium);
    auto plB = make_unique<PhysicalLayerInMemory>(idB, medium);
    ValidationConfig vc;
    sdkA = make_unique<EminentSdk>(std::move(plA), vc, LogLevel::NONE);
    sdkB = make_unique<EminentSdk>(std::move(plB), vc, LogLevel::NONE);

    sdkA->initialize(idA, [](){}, [](const string&){},
                     [](DeviceId, const string&){ return true; }, nullptr);
    // B rejects all connections
    sdkB->initialize(idB, [](){}, [](const string&){},
                     [](DeviceId, const string&){ return false; }, nullptr);

    atomic<bool> aFailed{false};
    atomic<bool> aConnected{false};

    sdkA->connect(
        idB, 5,
        [&](ConnectionId) { aConnected = true; },
        [&](const string&) { aFailed = true; },
        [](const string&) {},
        []() {},
        [&](ConnectionId) { aConnected = true; },
        [](const Message&) {},
        2000ms, nullptr, 5000ms
    );

    // Either A gets explicitly failed or handshake times out
    this_thread::sleep_for(6000ms);

    // A should NOT have an active connection
    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_TRUE(aIds.empty() || aFailed.load())
        << "Connection should have been rejected or timed out";
}

// ============================================================
// TEST: Handshake timeout (B never responds)
// ============================================================
TEST_F(HandshakeIntegration, HandshakeTimeout) {
    // Only A is in the medium, B does not exist (use a fresh medium)
    auto isolatedMedium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(idA, isolatedMedium);
    ValidationConfig vc;
    // Replace sdkA with one on the isolated medium
    sdkA->shutdown();
    this_thread::sleep_for(50ms);
    sdkA = make_unique<EminentSdk>(std::move(plA), vc, LogLevel::NONE);
    sdkA->initialize(idA, [](){}, [](const string&){},
                     [](DeviceId, const string&){ return true; }, nullptr);

    // B is shutdown/not on this medium, so no one answers
    sdkB->shutdown();
    sdkB.reset();

    atomic<bool> failed{false};
    string failReason;

    sdkA->connect(
        idB, 5,
        [](ConnectionId) { FAIL() << "Should not succeed"; },
        [&](const string& err) { failReason = err; failed = true; },
        [](const string&) {},
        []() {},
        [](ConnectionId) { FAIL() << "Should not connect"; },
        [](const Message&) {},
        2000ms, nullptr,
        3000ms  // Short handshake timeout
    );

    ASSERT_TRUE(waitFor(failed, 5000ms)) << "Expected handshake timeout failure";
    EXPECT_FALSE(failReason.empty());
}

// ============================================================
// TEST: Multiple simultaneous connections
// ============================================================
TEST_F(HandshakeIntegration, MultipleConnections) {
    // A, B, C all connect
    DeviceId idC = 300;
    auto plC = make_unique<PhysicalLayerInMemory>(idC, medium);
    ValidationConfig vc;
    auto sdkC = make_unique<EminentSdk>(std::move(plC), vc, LogLevel::NONE);

    initBoth();
    sdkC->initialize(idC, [](){}, [](const string&){},
                     [](DeviceId, const string&){ return true; }, nullptr);

    atomic<bool> abOk{false};
    atomic<bool> acOk{false};

    sdkA->connect(idB, 5,
        [&](ConnectionId) { abOk = true; },
        [](const string&) {},
        [](const string&) {},
        []() {},
        [&](ConnectionId) { abOk = true; },
        [](const Message&) {},
        2000ms, nullptr, 10000ms
    );

    sdkA->connect(idC, 5,
        [&](ConnectionId) { acOk = true; },
        [](const string&) {},
        [](const string&) {},
        []() {},
        [&](ConnectionId) { acOk = true; },
        [](const Message&) {},
        2000ms, nullptr, 10000ms
    );

    waitFor(abOk, 8000ms);
    waitFor(acOk, 8000ms);

    auto aIds = sdkA->getActiveConnectionIds();
    EXPECT_GE(aIds.size(), 1u) << "A should have at least one connection";

    sdkC->shutdown();
}
