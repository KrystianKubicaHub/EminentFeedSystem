#include "AbstractPhysicalLayer.hpp"

#include "CodingModule.hpp"

#include <stdexcept>

using namespace std;

AbstractPhysicalLayer::AbstractPhysicalLayer(const string& loggerName)
    : LoggerBase(loggerName) {}

void AbstractPhysicalLayer::setEnvironment(queue<Frame>& outgoingFrames,
                                           CodingModule& codingModule,
                                           const ValidationConfig& validationConfig) {
    outgoingFramesFromCodingModule_ = &outgoingFrames;
    codingModule_ = &codingModule;
    validationConfig_ = &validationConfig;
    computeFrameLayout();
}

void AbstractPhysicalLayer::computeFrameLayout() {
    if (!validationConfig_) {
        throw runtime_error("Physical layer not configured: validation config missing");
    }
    headerBytes_ = validationConfig_->transportHeaderBytes();
    payloadLimitBytes_ = validationConfig_->maxPayloadLengthBytes();

    maxFrameBytesWithoutCrc_ = headerBytes_ + payloadLimitBytes_;
    maxFrameBytesWithCrc_ = maxFrameBytesWithoutCrc_ + ValidationConfig::CRC_FIELD_BYTES;
}

void AbstractPhysicalLayer::ensureEncodableFrame(const Frame& frame) const {
    if (!isConfigured()) {
        throw runtime_error("Physical layer not configured");
    }
    if (frame.data.size() < headerBytes_) {
        throw runtime_error("Frame shorter than transport header");
    }
    if (frame.data.size() > maxFrameBytesWithoutCrc_) {
        throw runtime_error("Frame exceeds allowed payload size");
    }
}

void AbstractPhysicalLayer::ensureDecodableFrame(const Frame& frame) const {
    if (!isConfigured()) {
        throw runtime_error("Physical layer not configured");
    }
    if (frame.data.size() > maxFrameBytesWithCrc_) {
        throw runtime_error("Received frame exceeds configured limits");
    }
}
