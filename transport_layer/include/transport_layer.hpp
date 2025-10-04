#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <queue>
#include <common_types.hpp>




class SessionManager; // forward

class TransportLayer {
public:
    TransportLayer(std::queue<Package>& outgoingPackages, SessionManager& sessionManager);
    std::queue<Frame>& getOutgoingFrames();
    void receiveFrame(const Frame& frame); // nowa metoda

private:
    Frame serialize(const Package& pkg);
    Package deserialize(const Frame& frame);
    void appendBits(std::vector<uint8_t>& bits, uint64_t value, int bitCount);
    uint64_t readBits(const std::vector<uint8_t>& bits, size_t& offset, int bitCount);
    uint32_t crc32(const std::vector<uint8_t>& dataBytes);

    std::queue<Package>& outgoingPackages_;
    std::queue<Frame> outgoingFrames_;
    SessionManager& sessionManager_;
};
