#pragma once
#include <queue>
#include <cstdint>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <thread>
#include <atomic>

using namespace std;

class TransportLayer;

class CodingModule : public LoggerBase {
public:
    CodingModule(queue<Frame>& inputFrames, TransportLayer& transportLayer);
    ~CodingModule();
    queue<Frame>& getOutgoingFrames();
    void receiveFrameWithCrc(const Frame& frameWithCrc);

private:
    uint32_t crc32(const vector<uint8_t>& data);
    queue<Frame>& inputFrames_;
    queue<Frame> outgoingFrames_;
    TransportLayer& transportLayer_;
    thread worker_;
    atomic<bool> stopWorker_{false};
};
