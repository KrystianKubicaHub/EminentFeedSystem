#pragma once

#include <atomic>
#include <cstdint>
#include <netinet/in.h>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <ValidationConfig.hpp>

using namespace std;

class CodingModule;

class PhysicalLayerUdp : public LoggerBase {
public:
    static constexpr size_t FRAME_SIZE = 256;
    PhysicalLayerUdp(int localPort, const string& remoteHost, int remotePort,
                     queue<Frame>& outgoingFramesFromCodingModule,
                     CodingModule& codingModule,
                     const ValidationConfig& validationConfig);
    ~PhysicalLayerUdp();

    void tick();
    bool tryReceive(Frame& outFrame);
    void startWorkerLoop();

private:
    int sock_ = -1;
    int remotePort_;
    int localPort_;
    string remoteHost_;
    sockaddr_in remoteAddr_{};
    sockaddr_in localAddr_{};
    queue<Frame>& outgoingFramesFromCodingModule_;
    CodingModule& codingModule_;
    const ValidationConfig& validationConfig_;
    size_t maxEncodedFrameBytes_{};
    queue<Frame> incomingFrames_;
    thread worker_;
    atomic<bool> stopWorker_{false};
    void padFrame(Frame& frame) const;
    void unpadFrame(Frame& frame) const;
    void initializeConstraints();
    void ensureCanSend(const Frame& frame) const;
    void ensureCanReceive(const Frame& frame) const;
};
