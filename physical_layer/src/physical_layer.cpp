#include "physical_layer.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>


PhysicalLayerUdp::PhysicalLayerUdp(int localPort, const std::string& remoteHost, int remotePort)
    : remotePort_(remotePort), remoteHost_(remoteHost) {

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(localPort);

    if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock_);
        throw std::runtime_error("Failed to bind socket");
    }
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
}

PhysicalLayerUdp::~PhysicalLayerUdp() {
    if (sock_ >= 0) {
        close(sock_);
    }
}

void PhysicalLayerUdp::enqueueFrame(const Frame& frame) {
    outgoingFrames_.push(frame);
}

void PhysicalLayerUdp::tick() {
    while (!outgoingFrames_.empty()) {
        Frame f = outgoingFrames_.front();
        outgoingFrames_.pop();

        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(remotePort_);
        inet_pton(AF_INET, remoteHost_.c_str(), &remote.sin_addr);

        sendto(sock_, f.data(), f.size(), 0,
               (struct sockaddr*)&remote, sizeof(remote));
    }

    uint8_t buffer[2048];
    sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);

    ssize_t received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&sender, &senderLen);
    while (received > 0) {
        Frame f(buffer, buffer + received);
        incomingFrames_.push(f);

        received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                            (struct sockaddr*)&sender, &senderLen);
    }
}

bool PhysicalLayerUdp::tryReceive(Frame& outFrame) {
    if (incomingFrames_.empty()) return false;
    outFrame = incomingFrames_.front();
    incomingFrames_.pop();
    return true;
}
