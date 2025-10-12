#pragma once

#include <atomic>
#include <cstdint>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <commonTypes.hpp>
#include "AbstractPhysicalLayer.hpp"

using namespace std;

class PhysicalLayerUdp : public AbstractPhysicalLayer {
public:
    PhysicalLayerUdp(int localPort,
                     const string& remoteHost,
                     int remotePort);
    void configure(queue<Frame>& outgoingFramesFromCodingModule,
                   CodingModule& codingModule,
                   const ValidationConfig& validationConfig) override;
    ~PhysicalLayerUdp() override;

    void start() override;
    void tick() override;
    bool tryReceive(Frame& outFrame) override;

    int localPort() const { return localPort_; }
    int remotePort() const { return remotePort_; }
    const string& remoteHost() const { return remoteHost_; }

private:
    void workerLoop();
    int sock_ = -1;
    int remotePort_;
    int localPort_;
    string remoteHost_;
    sockaddr_in remoteAddr_{};
    sockaddr_in localAddr_{};
    queue<Frame> incomingFrames_;
    thread worker_;
    atomic<bool> stopWorker_{false};
    vector<uint8_t> recvBuffer_;
};
