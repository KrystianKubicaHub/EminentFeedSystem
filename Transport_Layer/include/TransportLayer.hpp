#pragma once

#include <atomic>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <logging.hpp>
#include <commonTypes.hpp>

using namespace std;

class SessionManager;

class TransportLayer : public LoggerBase {
public:
    TransportLayer(queue<Package>& outgoingPackages, SessionManager& sessionManager);
    ~TransportLayer();
    
    queue<Frame>& getOutgoingFrames();
    void receiveFrame(const Frame& frame);

private:
    Frame serialize(const Package& pkg);
    Package deserialize(const Frame& frame);
    void appendBytes(vector<uint8_t>& bytes, uint64_t value, int byteCount);
    uint64_t readBytes(const vector<uint8_t>& bytes, size_t& offset, int byteCount);
    uint32_t crc32(const vector<uint8_t>& dataBytes);
    void workerLoop();
    queue<Package>& outgoingPackages_;
    queue<Frame> outgoingFrames_;
    SessionManager& sessionManager_;
    thread worker_;
    atomic<bool> stopWorker_{false};
};
