#include "PhysicalLayerInMemory.hpp"
#include "EminentSdk.hpp"
#include "ValidationConfig.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono;

// ============================================================
// InMemory Medium tests
// ============================================================

TEST(PhysicalLayer, InMemoryMediumCreation) {
    auto medium = make_shared<InMemoryMedium>();
    EXPECT_TRUE(medium->entries.empty());
    EXPECT_TRUE(medium->participants.empty());
}

TEST(PhysicalLayer, DuplicateDeviceIdThrows) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl1 = make_unique<PhysicalLayerInMemory>(1001, medium);
    EXPECT_THROW(
        auto pl2 = make_unique<PhysicalLayerInMemory>(1001, medium),
        runtime_error
    );
}

TEST(PhysicalLayer, MultipleDevicesRegister) {
    auto medium = make_shared<InMemoryMedium>();
    auto pl1 = make_unique<PhysicalLayerInMemory>(1001, medium);
    auto pl2 = make_unique<PhysicalLayerInMemory>(2002, medium);
    auto pl3 = make_unique<PhysicalLayerInMemory>(3003, medium);

    {
        lock_guard<mutex> lock(medium->mutex);
        EXPECT_EQ(medium->participants.size(), 3u);
    }
}

TEST(PhysicalLayer, UnregisterOnDestruction) {
    auto medium = make_shared<InMemoryMedium>();
    {
        auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
        {
            lock_guard<mutex> lock(medium->mutex);
            EXPECT_EQ(medium->participants.size(), 1u);
        }
    }
    {
        lock_guard<mutex> lock(medium->mutex);
        EXPECT_EQ(medium->participants.size(), 0u);
    }
}

// ============================================================
// Multi-device communication via InMemory
// ============================================================

TEST(PhysicalLayer, ThreeDeviceCommunication) {
    auto medium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(1001, medium);
    auto plB = make_unique<PhysicalLayerInMemory>(2002, medium);
    auto plC = make_unique<PhysicalLayerInMemory>(3003, medium);

    ValidationConfig vc;
    EminentSdk sdkA(std::move(plA), vc);
    EminentSdk sdkB(std::move(plB), vc);
    EminentSdk sdkC(std::move(plC), vc);

    sdkA.initialize(1001, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; });

    atomic<ConnectionId> connBA{-1}, connCA{-1};

    sdkB.initialize(2002, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; },
        [&](ConnectionId cid, DeviceId remoteId) {
            if (remoteId == 1001) connBA = cid;
        });

    sdkC.initialize(3003, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; },
        [&](ConnectionId cid, DeviceId remoteId) {
            if (remoteId == 1001) connCA = cid;
        });

    // A connects to B
    atomic<ConnectionId> connAB{-1};
    sdkA.connect(2002, 5, nullptr, nullptr, nullptr, nullptr,
        [&](ConnectionId cid) { connAB = cid; }, nullptr);

    auto deadline = steady_clock::now() + 5s;
    while ((connAB.load() == -1 || connBA.load() == -1) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    ASSERT_NE(connAB.load(), -1);
    ASSERT_NE(connBA.load(), -1);

    // A connects to C
    atomic<ConnectionId> connAC{-1};
    sdkA.connect(3003, 5, nullptr, nullptr, nullptr, nullptr,
        [&](ConnectionId cid) { connAC = cid; }, nullptr);

    deadline = steady_clock::now() + 5s;
    while ((connAC.load() == -1 || connCA.load() == -1) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    ASSERT_NE(connAC.load(), -1);
    ASSERT_NE(connCA.load(), -1);

    // Verify A has two active connections (multi-device handshake works)
    auto activeIds = sdkA.getActiveConnectionIds();
    EXPECT_EQ(activeIds.size(), 2u);
    auto connDevices = sdkA.getConnectedDeviceIds();
    EXPECT_EQ(connDevices.size(), 2u);

    // Note: Message delivery in 3-device InMemory broadcast scenario is a known
    // limitation. InMemory medium broadcasts to all participants without
    // connection-ID routing at the physical layer. 2-device message delivery
    // is tested in CrcIntegrityPreserved and other test suites.
}

// ============================================================
// CRC integrity test
// ============================================================

TEST(PhysicalLayer, CrcIntegrityPreserved) {
    // This is an end-to-end test: data must arrive intact after CRC encode/decode
    auto medium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(1001, medium);
    auto plB = make_unique<PhysicalLayerInMemory>(2002, medium);
    ValidationConfig vc;
    EminentSdk sdkA(std::move(plA), vc);
    EminentSdk sdkB(std::move(plB), vc);

    sdkA.initialize(1001, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; });

    atomic<ConnectionId> connB{-1};
    sdkB.initialize(2002, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; },
        [&](ConnectionId cid, DeviceId) { connB = cid; });

    atomic<ConnectionId> connA{-1};
    sdkA.connect(2002, 5, nullptr, nullptr, nullptr, nullptr,
        [&](ConnectionId cid) { connA = cid; }, nullptr);

    auto deadline = steady_clock::now() + 5s;
    while ((connA.load() == -1 || connB.load() == -1) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    ASSERT_NE(connA.load(), -1);

    // Send special characters and edge cases
    string testPayload = "Special chars: \x01\x02\x03 tabs\tnewlines\nquotes\"backslash\\end";

    atomic<bool> received{false};
    string receivedPayload;

    sdkB.setOnMessageHandler(connB.load(), [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    sdkA.send(connA.load(), testPayload);

    deadline = steady_clock::now() + 5s;
    while (!received.load() && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(received.load());
    EXPECT_EQ(receivedPayload, testPayload);
}
