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
// Session Manager integration tests (fragmentation + ACK)
// ============================================================

TEST(SessionManager, LargeMessageFragmentation) {
    auto medium = make_shared<InMemoryMedium>();
    auto plA = make_unique<PhysicalLayerInMemory>(1001, medium);
    auto plB = make_unique<PhysicalLayerInMemory>(2002, medium);
    // Use small max payload to force fragmentation (8 bits = max 255 fragments, small packet)
    ValidationConfig vc(16, 16, 24, 24, 8, 8, 4, 16);
    EminentSdk sdkA(std::move(plA), vc);
    EminentSdk sdkB(std::move(plB), vc);

    sdkA.initialize(1001, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; });

    atomic<ConnectionId> connB{-1};
    sdkB.initialize(2002, [](){}, [](const string&){},
        [](DeviceId, const string&) { return true; },
        [&](ConnectionId cid, DeviceId) {
            connB = cid;
            sdkB.setOnMessageHandler(cid, [](const Message&){});
        });

    atomic<ConnectionId> connA{-1};
    sdkA.connect(2002, 5, nullptr, nullptr, nullptr, nullptr,
        [&](ConnectionId cid) { connA = cid; }, nullptr);

    auto deadline = steady_clock::now() + 5s;
    while ((connA.load() == -1 || connB.load() == -1) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    ASSERT_NE(connA.load(), -1);

    // Create a large payload (bigger than maxPayloadLengthBytes)
    // Default maxPayload = 65535, so we'll use a big string
    string largePayload(5000, 'X');

    atomic<bool> received{false};
    string receivedPayload;

    sdkB.setOnMessageHandler(connB.load(), [&](const Message& msg) {
        receivedPayload = msg.payload;
        received = true;
    });

    atomic<bool> delivered{false};
    sdkA.send(connA.load(), largePayload, MessageFormat::JSON, 5, true,
        [&]() { delivered = true; });

    deadline = steady_clock::now() + 10s;
    while ((!received.load() || !delivered.load()) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(received.load());
    EXPECT_TRUE(delivered.load());
    EXPECT_EQ(receivedPayload, largePayload);
}

TEST(SessionManager, MultipleMessagesInOrder) {
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

    constexpr int MSG_COUNT = 10;
    atomic<int> receivedCount{0};
    vector<string> receivedMessages;
    mutex receivedMutex;

    sdkB.setOnMessageHandler(connB.load(), [&](const Message& msg) {
        lock_guard<mutex> lock(receivedMutex);
        receivedMessages.push_back(msg.payload);
        receivedCount++;
    });

    for (int i = 0; i < MSG_COUNT; ++i) {
        sdkA.send(connA.load(), "msg_" + to_string(i));
    }

    deadline = steady_clock::now() + 10s;
    while (receivedCount.load() < MSG_COUNT && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    EXPECT_EQ(receivedCount.load(), MSG_COUNT);
}

TEST(SessionManager, AckDeliveryCallbacks) {
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
        [&](ConnectionId cid, DeviceId) {
            connB = cid;
            sdkB.setOnMessageHandler(cid, [](const Message&){});
        });

    atomic<ConnectionId> connA{-1};
    sdkA.connect(2002, 5, nullptr, nullptr, nullptr, nullptr,
        [&](ConnectionId cid) { connA = cid; }, nullptr);

    auto deadline = steady_clock::now() + 5s;
    while ((connA.load() == -1 || connB.load() == -1) && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    ASSERT_NE(connA.load(), -1);

    constexpr int MSG_COUNT = 5;
    atomic<int> deliveredCount{0};

    for (int i = 0; i < MSG_COUNT; ++i) {
        sdkA.send(connA.load(), "ack_test_" + to_string(i),
            [&]() { deliveredCount++; });
    }

    deadline = steady_clock::now() + 10s;
    while (deliveredCount.load() < MSG_COUNT && steady_clock::now() < deadline) {
        this_thread::sleep_for(50ms);
    }
    EXPECT_EQ(deliveredCount.load(), MSG_COUNT);
}
