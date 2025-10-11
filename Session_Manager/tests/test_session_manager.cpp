#include "SessionManager.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

TEST(SessionManagerTest, EnqueueAndFragmentation) {
    SessionManager sm(5);
    sm.enqueueMessage(1, 100, "HelloWorld", MessageFormat::JSON, 5, true);

    Package pkg;
    int count = 0;
    while (sm.getNextPackage(pkg)) {
        count++;
    }
    EXPECT_EQ(count, 2);
}

TEST(SessionManagerTest, AckRemovesFromInFlight) {
    SessionManager sm(5);
    sm.enqueueMessage(1, 100, "HelloWorld", MessageFormat::JSON, 5, true);

    Package pkg;
    ASSERT_TRUE(sm.getNextPackage(pkg));
    sm.ackPackage(pkg.packageId);

    auto stats = sm.getStats();
    EXPECT_EQ(stats.inFlight, 0);
}

TEST(SessionManagerTest, RetryAfterTimeout) {
    SessionManager sm(5);
    sm.enqueueMessage(1, 100, "Hello", MessageFormat::JSON, 5, true);

    Package pkg;
    ASSERT_TRUE(sm.getNextPackage(pkg));

    std::this_thread::sleep_for(20ms);
    sm.checkTimeouts(10);

    Package retryPkg;
    ASSERT_TRUE(sm.getNextPackage(retryPkg));
    EXPECT_EQ(retryPkg.packageId, pkg.packageId);
    EXPECT_GT(retryPkg.retryCount, 0);
}

TEST(SessionManagerTest, StatsCountRetransmissions) {
    SessionManager sm(5);
    sm.enqueueMessage(1, 100, "Hello", MessageFormat::JSON, 5, true);

    Package pkg;
    ASSERT_TRUE(sm.getNextPackage(pkg));

    std::this_thread::sleep_for(20ms);
    sm.checkTimeouts(10);

    auto stats = sm.getStats();
    EXPECT_EQ(stats.retransmissions, 1);
}
