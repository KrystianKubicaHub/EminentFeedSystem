#include "eminent_sdk.hpp"
#include <gtest/gtest.h>
#include <string>

using std::string;

TEST(EminentSdkTest, InitializeHandshakeQueued) {
    EminentSdk sdk;

    bool initSuccess = false;
    bool initFailure = false;

    sdk.initialize(
        1001,
        [&]() { initSuccess = true; },
        [&](const string&) { initFailure = true; },
        [](DeviceId) {}
    );

    EXPECT_TRUE(initSuccess);
    EXPECT_FALSE(initFailure);
}

TEST(EminentSdkTest, ConnectSuccess) {
    EminentSdk sdk;

    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    bool successCalled = false;
    sdk.connect(
        2002, 5,
        [&](ConnectionId cid) {
            successCalled = true;
            EXPECT_EQ(cid, 1);
        },
        [&](const string& err) {
            FAIL() << "Unexpected failure: " << err;
        },
        [&](const string&) { SUCCEED(); },
        [&]() { SUCCEED(); }
    );

    EXPECT_TRUE(successCalled);
}

TEST(EminentSdkTest, ConnectFailureInvalidId) {
    EminentSdk sdk;
    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    bool failureCalled = false;
    sdk.connect(
        -5, 5,
        [](ConnectionId) { FAIL() << "Should not succeed"; },
        [&](const string& err) { failureCalled = true; EXPECT_FALSE(err.empty()); },
        [](const string&) {},
        []() {}
    );

    EXPECT_TRUE(failureCalled);
}

TEST(EminentSdkTest, SendAndReceiveJsonMessage) {
    EminentSdk sdk;
    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    ConnectionId cid = -1;
    sdk.connect(
        2002, 5,
        [&](ConnectionId newCid) { cid = newCid; },
        [](const string& err) { FAIL() << "Unexpected failure: " << err; },
        [](const string&) {},
        []() {}
    );

    ASSERT_NE(cid, -1);

    bool delivered = false;
    sdk.send(
        cid, R"({"key": "value"})", MessageFormat::JSON,
        5, true,
        [](MessageId mid) { EXPECT_GT(mid, 0); },
        [&]() { delivered = true; }
    );

    EXPECT_TRUE(delivered);
}

TEST(EminentSdkTest, ListenReceivesMessage) {
    EminentSdk sdk;
    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    ConnectionId cid = -1;
    sdk.connect(
        2002, 5,
        [&](ConnectionId newCid) { cid = newCid; },
        [](const string& err) { FAIL() << "Unexpected failure: " << err; },
        [](const string&) {},
        []() {}
    );

    ASSERT_NE(cid, -1);

    bool received = false;
    sdk.listen(cid, [&](const string& msg) {
        received = true;
        EXPECT_EQ(msg, "Hello");
    });

    sdk.send(
        cid, "Hello", MessageFormat::JSON, 5, false,
        [](MessageId) {}, []() {}
    );

    EXPECT_TRUE(received);
}

TEST(EminentSdkTest, CloseConnection) {
    EminentSdk sdk;
    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    ConnectionId cid = -1;
    sdk.connect(
        2002, 5,
        [&](ConnectionId newCid) { cid = newCid; },
        [](const string& err) { FAIL() << "Unexpected failure: " << err; },
        [](const string&) {},
        []() {}
    );

    ASSERT_NE(cid, -1);

    sdk.close(cid);

    sdk.send(
        cid, "ShouldFail", MessageFormat::JSON, 5, false,
        [](MessageId) { FAIL() << "Message should not be queued"; },
        []() { FAIL() << "Message should not be delivered"; }
    );
}

TEST(EminentSdkTest, GetStatsWorks) {
    EminentSdk sdk;
    sdk.initialize(1001, [](){}, [](const string&){}, [](DeviceId){});

    ConnectionId cid1 = -1;
    sdk.connect(2002, 5, [&](ConnectionId c){ cid1 = c; }, [](const string&){}, [](const string&){}, [](){});
    ASSERT_NE(cid1, -1);

    ConnectionId cid2 = -1;
    sdk.connect(2003, 7, [&](ConnectionId c){ cid2 = c; }, [](const string&){}, [](const string&){}, [](){});
    ASSERT_NE(cid2, -1);

    sdk.getStats([&](const std::vector<ConnectionStats>& stats){
        EXPECT_EQ(stats.size(), 2);
        EXPECT_TRUE(stats[0].id == cid1 || stats[1].id == cid1);
        EXPECT_TRUE(stats[0].id == cid2 || stats[1].id == cid2);
    });
}
