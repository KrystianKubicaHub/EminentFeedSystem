#pragma once

// ============================================================
// PhysicalLayerEsp32Wifi — UDP transport for ESP-IDF (ESP32)
//
// Uses lwIP BSD sockets (available in ESP-IDF).
// Before constructing this object, WiFi must already be
// connected (STA mode, IP obtained).
// ============================================================

#ifdef ESP_PLATFORM

#include <atomic>
#include <cstdint>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <commonTypes.hpp>
#include "AbstractPhysicalLayer.hpp"

class PhysicalLayerEsp32Wifi : public AbstractPhysicalLayer {
public:
    /**
     * @param localPort  UDP port to bind (receive on this port)
     * @param remoteHost IP address of the remote peer (e.g. "192.168.4.1")
     * @param remotePort UDP port of the remote peer
     */
    PhysicalLayerEsp32Wifi(int localPort,
                           const std::string& remoteHost,
                           int remotePort);

    ~PhysicalLayerEsp32Wifi() override;

    void configure(ThreadSafeQueue<Frame>& outgoingFramesFromCodingModule,
                   CodingModule& codingModule,
                   const ValidationConfig& validationConfig) override;

    void start() override;
    void tick() override;
    bool tryReceive(Frame& outFrame) override;

    int localPort() const { return localPort_; }
    int remotePort() const { return remotePort_; }
    const std::string& remoteHost() const { return remoteHost_; }

private:
    void workerLoop();

    int sock_ = -1;
    int localPort_;
    int remotePort_;
    std::string remoteHost_;
    struct sockaddr_in remoteAddr_{};
    struct sockaddr_in localAddr_{};
    std::queue<Frame> incomingFrames_;
    std::thread worker_;
    std::atomic<bool> stopWorker_{false};
    std::vector<uint8_t> recvBuffer_;
};

#endif // ESP_PLATFORM
