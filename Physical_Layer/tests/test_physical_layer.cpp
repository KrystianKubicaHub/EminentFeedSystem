#include "PhysicalLayer.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

TEST(PhysicalLayerUdpTest, SendAndReceiveBetweenTwoInstances) {
    PhysicalLayerUdp devA(6000, "127.0.0.1", 6001);
    PhysicalLayerUdp devB(6001, "127.0.0.1", 6000);

    Frame f = {1,2,3,4,5};
    devA.enqueueFrame(f);

    for (int i = 0; i < 10; i++) {
        devA.tick();
        devB.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Frame out;
    ASSERT_TRUE(devB.tryReceive(out));
    EXPECT_EQ(out, f);
}
