#pragma once

#include <vector>
#include <queue>
#include <cstdint>
#include <string>
#include <netinet/in.h>


#include <common_types.hpp>
#include <thread>
#include <atomic>

class CodingModule; // forward

class CodingModule; // forward

class PhysicalLayerUdp {
public:
    static constexpr size_t FRAME_SIZE = 256;
    PhysicalLayerUdp(int localPort, const std::string& remoteHost, int remotePort, std::queue<Frame>& outgoingFramesFromCodingModule, CodingModule& codingModule);
    ~PhysicalLayerUdp();

    void tick(); // obsługuje wysyłanie i odbiór
    bool tryReceive(Frame& outFrame); // pobiera odebraną ramkę
    void startWorkerLoop();

private:
    int sock_ = -1;
    int remotePort_;
    int localPort_;
    std::string remoteHost_;
    sockaddr_in remoteAddr_{};
    sockaddr_in localAddr_{};

    std::queue<Frame>& outgoingFramesFromCodingModule_;
    CodingModule& codingModule_;
    std::queue<Frame> incomingFrames_;
    std::thread worker_;
    std::atomic<bool> stopWorker_{false};
    void padFrame(Frame& frame);
    void unpadFrame(Frame& frame);
};
