#include "TransportLayer.hpp"
#include <gtest/gtest.h>
#include <string>

using std::string;

TEST(TransportLayerTest, SerializeDeserializeRoundTrip) {
    TransportLayer tl;

    Package pkg{};
    pkg.packageId = 42;
    pkg.messageId = 99;
    pkg.senderId = 1001;
    pkg.receiverId = 2002;
    pkg.format = MessageFormat::JSON;
    pkg.requireAck = true;
    pkg.priority = 7;
    pkg.payload = R"({"hello": "world"})";

    Frame frame = tl.serialize(pkg);
    Package decoded = tl.deserialize(frame);

    EXPECT_EQ(decoded.packageId, pkg.packageId);
    EXPECT_EQ(decoded.messageId, pkg.messageId);
    EXPECT_EQ(decoded.senderId, pkg.senderId);
    EXPECT_EQ(decoded.receiverId, pkg.receiverId);
    EXPECT_EQ(decoded.format, pkg.format);
    EXPECT_EQ(decoded.requireAck, pkg.requireAck);
    EXPECT_EQ(decoded.priority, pkg.priority);
    EXPECT_EQ(decoded.payload, pkg.payload);
}

TEST(TransportLayerTest, DeserializeWithBadCrcThrows) {
    TransportLayer tl;

    Package pkg{};
    pkg.packageId = 1;
    pkg.messageId = 2;
    pkg.senderId = 10;
    pkg.receiverId = 20;
    pkg.format = MessageFormat::JSON;
    pkg.requireAck = false;
    pkg.priority = 1;
    pkg.payload = "data";

    Frame frame = tl.serialize(pkg);

    frame.bits.back() ^= 1;

    EXPECT_THROW({
        tl.deserialize(frame);
    }, std::runtime_error);
}

TEST(TransportLayerTest, SerializeDeserializeVideo) {
    TransportLayer tl;

    Package pkg{};
    pkg.packageId = 5;
    pkg.messageId = 6;
    pkg.senderId = 123;
    pkg.receiverId = 456;
    pkg.format = MessageFormat::VIDEO;
    pkg.requireAck = true;
    pkg.priority = 3;
    pkg.payload = string("\x01\x02\x03\x04", 4);

    Frame frame = tl.serialize(pkg);
    Package decoded = tl.deserialize(frame);

    EXPECT_EQ(decoded.format, MessageFormat::VIDEO);
    EXPECT_EQ(decoded.payload.size(), 4);
    EXPECT_EQ(decoded.payload[0], '\x01');
    EXPECT_EQ(decoded.payload[3], '\x04');
}

TEST(TransportLayerTest, RequireAckFalse) {
    TransportLayer tl;

    Package pkg{};
    pkg.packageId = 7;
    pkg.messageId = 8;
    pkg.senderId = 77;
    pkg.receiverId = 88;
    pkg.format = MessageFormat::HANDSHAKE;
    pkg.requireAck = false;
    pkg.priority = 9;
    pkg.payload = "HELLO";

    Frame frame = tl.serialize(pkg);
    Package decoded = tl.deserialize(frame);

    EXPECT_FALSE(decoded.requireAck);
    EXPECT_EQ(decoded.payload, "HELLO");
}

TEST(TransportLayerTest, DeserializeWithBadSyncThrows) {
    TransportLayer tl;

    Package pkg{};
    pkg.packageId = 9;
    pkg.messageId = 10;
    pkg.senderId = 11;
    pkg.receiverId = 12;
    pkg.format = MessageFormat::JSON;
    pkg.requireAck = true;
    pkg.priority = 1;
    pkg.payload = "ABC";

    Frame frame = tl.serialize(pkg);

    frame.bits[0] ^= 1;

    EXPECT_THROW({
        tl.deserialize(frame);
    }, std::runtime_error);
}
