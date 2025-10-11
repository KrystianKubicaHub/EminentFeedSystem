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

using namespace std;

class CodingModule;

class PhysicalLayerUdp : public LoggerBase {
public:
    static constexpr size_t FRAME_SIZE = 256;
    PhysicalLayerUdp(int localPort, const string& remoteHost, int remotePort, queue<Frame>& outgoingFramesFromCodingModule, CodingModule& codingModule);
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
    queue<Frame> incomingFrames_;
    thread worker_;
    atomic<bool> stopWorker_{false};
    void padFrame(Frame& frame);
    void unpadFrame(Frame& frame);
};
