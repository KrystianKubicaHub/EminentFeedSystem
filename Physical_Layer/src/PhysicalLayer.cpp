#include "PhysicalLayer.hpp"
#include "CodingModule.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <chrono>
#include <sstream>

using namespace std;
using namespace chrono;

PhysicalLayerUdp::PhysicalLayerUdp(int localPort, const string& remoteHost, int remotePort, queue<Frame>& outgoingFramesFromCodingModule, CodingModule& codingModule)
    : LoggerBase("PhysicalLayer"), remotePort_(remotePort), localPort_(localPort), remoteHost_(remoteHost),
      outgoingFramesFromCodingModule_(outgoingFramesFromCodingModule), codingModule_(codingModule) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw runtime_error("Failed to create socket");
    }

    memset(&localAddr_, 0, sizeof(localAddr_));
    localAddr_.sin_family = AF_INET;
    localAddr_.sin_addr.s_addr = INADDR_ANY;
    localAddr_.sin_port = htons(localPort);
    if (bind(sock_, reinterpret_cast<struct sockaddr*>(&localAddr_), sizeof(localAddr_)) < 0) {
        close(sock_);
        throw runtime_error("Failed to bind socket");
    }

    memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteHost.c_str(), &remoteAddr_.sin_addr);
    fcntl(sock_, F_SETFL, O_NONBLOCK);
    startWorkerLoop();
}

void PhysicalLayerUdp::startWorkerLoop() {
    worker_ = thread([this]() {
        try {
            while (!stopWorker_) {
                while (!outgoingFramesFromCodingModule_.empty()) {
                    Frame frame = outgoingFramesFromCodingModule_.front();
                    outgoingFramesFromCodingModule_.pop();
                    ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                                           reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
                    if (sent < 0) {
                        log(LogLevel::ERROR, "Failed to send frame over UDP");
                        continue;
                    }
                    log(LogLevel::DEBUG, string("Sent frame size=") + to_string(frame.data.size()));
                }

                uint8_t buffer[FRAME_SIZE];
                sockaddr_in sender{};
                socklen_t senderLen = sizeof(sender);
                ssize_t received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                            reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
                while (received > 0) {
                    Frame frame;
                    frame.data.assign(buffer, buffer + received);
                    unpadFrame(frame);
                    log(LogLevel::DEBUG, string("Received frame size=") + to_string(received));
                    codingModule_.receiveFrameWithCrc(frame);
                    received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
                }

                this_thread::sleep_for(10ms);
            }
        } catch (const exception& ex) {
            log(LogLevel::ERROR, string("Worker exception: ") + ex.what());
        } catch (...) {
            log(LogLevel::ERROR, "Worker exception: unknown exception");
        }
    });
}

PhysicalLayerUdp::~PhysicalLayerUdp() {
    log(LogLevel::DEBUG, "Destructor invoked, stopping worker");
    stopWorker_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (sock_ >= 0) {
        close(sock_);
    }
    log(LogLevel::DEBUG, "Worker stopped");
}

void PhysicalLayerUdp::tick() {
    while (!outgoingFramesFromCodingModule_.empty()) {
        Frame frame = outgoingFramesFromCodingModule_.front();
        outgoingFramesFromCodingModule_.pop();
        ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                               reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
        if (sent < 0) {
            log(LogLevel::ERROR, "Failed to send frame during tick");
        } else {
            log(LogLevel::DEBUG, string("Tick sent frame size=") + to_string(frame.data.size()));
        }
    }

    uint8_t buffer[FRAME_SIZE];
    sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);
    ssize_t received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
    while (received > 0) {
        Frame frame;
        frame.data.assign(buffer, buffer + received);
        unpadFrame(frame);
        log(LogLevel::DEBUG, string("Tick received frame size=") + to_string(received));
        codingModule_.receiveFrameWithCrc(frame);
        received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                            reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
    }
}

bool PhysicalLayerUdp::tryReceive(Frame& outFrame) {
    if (incomingFrames_.empty()) {
        return false;
    }
    outFrame = incomingFrames_.front();
    incomingFrames_.pop();
    return true;
}

void PhysicalLayerUdp::padFrame(Frame& frame) {
    if (frame.data.size() < FRAME_SIZE) {
        frame.data.resize(FRAME_SIZE, 0);
    }
}

void PhysicalLayerUdp::unpadFrame(Frame& frame) {
    (void)frame;
}
