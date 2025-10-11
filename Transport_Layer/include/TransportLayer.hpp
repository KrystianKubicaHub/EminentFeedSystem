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
#include <ValidationConfig.hpp>

using namespace std;

class SessionManager;

class TransportLayer : public LoggerBase {
public:
    TransportLayer(queue<Package>& outgoingPackages, SessionManager& sessionManager, const ValidationConfig& validationConfig);
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
    const ValidationConfig& validationConfig_;
    uint8_t packageIdBytes_{};
    uint8_t messageIdBytes_{};
    uint8_t connectionIdBytes_{};
    uint8_t fragmentIdBytes_{};
    uint8_t fragmentsCountBytes_{};
    uint8_t priorityBytes_{};
    uint8_t requireAckBytes_{};
    uint8_t formatBytes_{};
    uint8_t payloadLengthBytes_{};
    uint64_t packageIdMax_{};
    uint64_t messageIdMax_{};
    uint64_t connectionIdMax_{};
    uint64_t fragmentIdMax_{};
    uint64_t fragmentsCountMax_{};
    uint64_t priorityMax_{};
    void initializeFieldWidths();
    void validateSerializedPackage(const Package& pkg) const;
    void validateDeserializedPackage(const Package& pkg) const;
    thread worker_;
    atomic<bool> stopWorker_{false};
};
