#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <vector>
#include <thread>

#include <commonTypes.hpp>

#include "AbstractPhysicalLayer.hpp"

struct InMemoryMediumEntry {
    DeviceId senderId;
    Frame frame;
    std::unordered_set<DeviceId> deliveredTo;
};

struct InMemoryMedium {
    std::mutex mutex;
    std::vector<InMemoryMediumEntry> entries;
    std::unordered_set<DeviceId> participants;
};

class PhysicalLayerInMemory : public AbstractPhysicalLayer {
public:
    PhysicalLayerInMemory(DeviceId selfId,
                          std::shared_ptr<InMemoryMedium> medium);
    ~PhysicalLayerInMemory() override;

    void configure(std::queue<Frame>& outgoingFramesFromCodingModule,
                   CodingModule& codingModule,
                   const ValidationConfig& validationConfig) override;

    void start() override;
    void tick() override;
    bool tryReceive(Frame& outFrame) override;

private:
    void registerParticipant();
    void unregisterParticipant();
    void processOutgoingFrames();
    void processIncomingFrames();
    void workerLoop();

    DeviceId selfId_;
    std::shared_ptr<InMemoryMedium> medium_;
    std::queue<Frame> incomingFrames_;
    std::atomic<bool> stopWorker_{false};
    std::thread worker_;
};
