#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <ValidationConfig.hpp>

class CodingModule;

using namespace std;

class AbstractPhysicalLayer : public LoggerBase {
public:
    explicit AbstractPhysicalLayer(const string& loggerName);
    virtual ~AbstractPhysicalLayer() = default;

    virtual void configure(queue<Frame>& outgoingFrames,
                           CodingModule& codingModule,
                           const ValidationConfig& validationConfig) = 0;

    virtual void start() = 0;
    virtual void tick() = 0;
    virtual bool tryReceive(Frame& outFrame) = 0;

protected:
    queue<Frame>* outgoingFramesFromCodingModule_{nullptr};
    CodingModule* codingModule_{nullptr};
    const ValidationConfig* validationConfig_{nullptr};

    size_t headerBytes_{};
    size_t payloadLimitBytes_{};
    size_t maxFrameBytesWithoutCrc_{};
    size_t maxFrameBytesWithCrc_{};

    void setEnvironment(queue<Frame>& outgoingFrames,
                        CodingModule& codingModule,
                        const ValidationConfig& validationConfig);

    bool isConfigured() const { return outgoingFramesFromCodingModule_ != nullptr; }

    void computeFrameLayout();
    void ensureEncodableFrame(const Frame& frame) const;
    void ensureDecodableFrame(const Frame& frame) const;

    size_t headerBytes() const { return headerBytes_; }
    size_t payloadLimitBytes() const { return payloadLimitBytes_; }
    size_t maxFrameBytesWithoutCrc() const { return maxFrameBytesWithoutCrc_; }
    size_t maxFrameBytesWithCrc() const { return maxFrameBytesWithCrc_; }
};
