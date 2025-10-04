#pragma once

#include <vector>
#include <queue>
#include <cstdint>
#include <string>


#include <common_types.hpp>

class PhysicalLayerUdp {
public:
    PhysicalLayerUdp(int localPort, const std::string& remoteHost, int remotePort);
    ~PhysicalLayerUdp();

    void enqueueFrame(const Frame& frame);
    void tick();                 
    bool tryReceive(Frame& outFrame);

private:
    int sock_;
    int remotePort_;
    std::string remoteHost_;

    std::queue<Frame> outgoingFrames_;
    std::queue<Frame> incomingFrames_;
};
