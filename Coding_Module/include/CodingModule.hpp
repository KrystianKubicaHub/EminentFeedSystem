#pragma once
#include <queue>
#include <cstdint>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <thread>
#include <atomic>
#include <ValidationConfig.hpp>
#include <ThreadSafeQueue.hpp>

using namespace std;

class TransportLayer;

class CodingModule : public LoggerBase {
public:
    CodingModule(ThreadSafeQueue<Frame>& inputFrames, TransportLayer& transportLayer, const ValidationConfig& validationConfig);
    ~CodingModule();
    ThreadSafeQueue<Frame>& getOutgoingFrames();
    void receiveFrameWithCrc(const Frame& frameWithCrc);

private:
    uint32_t crc32(const vector<uint8_t>& data);
    void initializeConstraints();
    void ensureFrameEncodable(const Frame& frame) const;
    void ensureFrameDecodable(const Frame& frameWithCrc) const;
    ThreadSafeQueue<Frame>& inputFrames_;
    ThreadSafeQueue<Frame> outgoingFrames_;
    TransportLayer& transportLayer_;
    const ValidationConfig& validationConfig_;
    size_t headerBytesWithoutPayload_{};
    size_t maxPayloadBytes_{};
    size_t maxFrameBytesWithoutCrc_{};
    size_t maxFrameBytesWithCrc_{};
    uint8_t payloadLengthBytes_{};
    static constexpr size_t CRC_BYTES = 4;
    thread worker_;
    atomic<bool> stopWorker_{false};
};
