#include "TransportLayer.hpp"
#include "SessionManager.hpp"
#include "EminentSdk.hpp"
#include "PhysicalLayerInMemory.hpp"
#include "ValidationConfig.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace std;

// ============================================================
// ValidationConfig tests
// ============================================================

TEST(ValidationConfig, DefaultConfigValid) {
    ValidationConfig vc;
    EXPECT_NO_THROW(vc.validateDeviceId(1));
    EXPECT_NO_THROW(vc.validateDeviceId(65535));
    EXPECT_NO_THROW(vc.validateConnectionId(1));
    EXPECT_NO_THROW(vc.validatePriority(0));
    EXPECT_NO_THROW(vc.validatePriority(15));
}

TEST(ValidationConfig, InvalidDeviceIdThrows) {
    ValidationConfig vc;
    EXPECT_THROW(vc.validateDeviceId(0), invalid_argument);
    EXPECT_THROW(vc.validateDeviceId(-1), invalid_argument);
}

TEST(ValidationConfig, PriorityOverflowThrows) {
    ValidationConfig vc; // 4 bits = max 15
    EXPECT_THROW(vc.validatePriority(16), invalid_argument);
    EXPECT_THROW(vc.validatePriority(-1), invalid_argument);
}

TEST(ValidationConfig, CustomBitWidths) {
    ValidationConfig vc(8, 8, 8, 8, 4, 4, 2, 8); // priority 2 bits = max 3
    EXPECT_NO_THROW(vc.validatePriority(3));
    EXPECT_THROW(vc.validatePriority(4), invalid_argument);
    EXPECT_NO_THROW(vc.validateDeviceId(255));
    EXPECT_THROW(vc.validateDeviceId(256), invalid_argument);
}

TEST(ValidationConfig, TransportHeaderBytesCalculation) {
    ValidationConfig vc;
    size_t headerBytes = vc.transportHeaderBytes();
    // Default: packageId(3) + messageId(3) + connectionId(2) + fragmentId(1) +
    // fragmentsCount(1) + priority(1) + format(1) + requireAck(1) + payloadLength(2) = 15
    EXPECT_EQ(headerBytes, 15u);
}

TEST(ValidationConfig, MaxPayloadLength) {
    ValidationConfig vc;
    // payloadLength is 2 bytes = max 65535
    EXPECT_EQ(vc.maxPayloadLengthBytes(), 65535u);
}

// ============================================================
// TransportLayer serialization tests
// ============================================================

class TransportLayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        medium = make_shared<InMemoryMedium>();
        auto pl = make_unique<PhysicalLayerInMemory>(1001, medium);
        sdk = make_unique<EminentSdk>(std::move(pl), vc);
        sdk->initialize(1001, [](){}, [](const string&){},
            [](DeviceId, const string&) { return true; });
    }

    ValidationConfig vc;
    shared_ptr<InMemoryMedium> medium;
    unique_ptr<EminentSdk> sdk;
};

TEST_F(TransportLayerTest, PackageValidation) {
    Package pkg{1, 1, 1, 0, 1, "hello", MessageFormat::JSON, 5, true, PackageStatus::QUEUED};
    EXPECT_NO_THROW(vc.validatePackage(pkg));
}

TEST_F(TransportLayerTest, PackageInvalidPriority) {
    Package pkg{1, 1, 1, 0, 1, "hello", MessageFormat::JSON, 99, true, PackageStatus::QUEUED};
    EXPECT_THROW(vc.validatePackage(pkg), invalid_argument);
}

TEST_F(TransportLayerTest, PackageInvalidFragmentId) {
    // fragmentId is 8 bits = max 255
    Package pkg{1, 1, 1, 256, 1, "hello", MessageFormat::JSON, 5, true, PackageStatus::QUEUED};
    EXPECT_THROW(vc.validatePackage(pkg), invalid_argument);
}

TEST_F(TransportLayerTest, MessageValidation) {
    Message msg{1, 1, "payload", MessageFormat::JSON, 5, true, nullptr};
    EXPECT_NO_THROW(vc.validateMessage(msg));
}

TEST_F(TransportLayerTest, MessageInvalidPriority) {
    Message msg{1, 1, "payload", MessageFormat::JSON, 20, true, nullptr};
    EXPECT_THROW(vc.validateMessage(msg), invalid_argument);
}
