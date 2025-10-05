#include "physical_layer.hpp"
#include "CodingModule.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>


PhysicalLayerUdp::PhysicalLayerUdp(int localPort, const std::string& remoteHost, int remotePort, std::queue<Frame>& outgoingFramesFromCodingModule, CodingModule& codingModule)
    : remotePort_(remotePort), localPort_(localPort), remoteHost_(remoteHost), outgoingFramesFromCodingModule_(outgoingFramesFromCodingModule), codingModule_(codingModule) {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) throw std::runtime_error("Failed to create socket");

    memset(&localAddr_, 0, sizeof(localAddr_));
    localAddr_.sin_family = AF_INET;
    localAddr_.sin_addr.s_addr = INADDR_ANY;
    localAddr_.sin_port = htons(localPort);
    if (bind(sock_, (struct sockaddr*)&localAddr_, sizeof(localAddr_)) < 0) {
        close(sock_);
        throw std::runtime_error("Failed to bind socket");
    }

    memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteHost.c_str(), &remoteAddr_.sin_addr);
    fcntl(sock_, F_SETFL, O_NONBLOCK);
        startWorkerLoop(); // wywołanie nowej metody
}

    void PhysicalLayerUdp::startWorkerLoop() {
            worker_ = std::thread([this]() {
                try {
                    while (!stopWorker_) {
                        // Wysyłanie ramek z kolejki CodingModule
                        while (!outgoingFramesFromCodingModule_.empty()) {
                            Frame frame = outgoingFramesFromCodingModule_.front();
                            outgoingFramesFromCodingModule_.pop();
                            sendto(sock_, frame.data.data(), frame.data.size(), 0,
                                   (struct sockaddr*)&remoteAddr_, sizeof(remoteAddr_));
                            std::cout << "[PhysicalLayer] Sent frame, size=" << frame.data.size() << std::endl;
                        }
                        // Odbiór ramek
                        uint8_t buffer[FRAME_SIZE];
                        sockaddr_in sender{};
                        socklen_t senderLen = sizeof(sender);
                        ssize_t received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                                    (struct sockaddr*)&sender, &senderLen);
                        while (received > 0) {
                            Frame frame;
                            frame.data.assign(buffer, buffer + received);
                            unpadFrame(frame);
                            std::cout << "[PhysicalLayer] Received frame, size=" << received << std::endl;
                            // Przekazanie ramki do CodingModule
                            codingModule_.receiveFrameWithCrc(frame);
                            received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                                (struct sockaddr*)&sender, &senderLen);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[PhysicalLayer][EXCEPTION] " << ex.what() << std::endl;
                } catch (...) {
                    std::cerr << "[PhysicalLayer][EXCEPTION] unknown exception" << std::endl;
                }
            });
    }
PhysicalLayerUdp::~PhysicalLayerUdp() {
    std::cout << "[PhysicalLayer] Destructor called, stopping worker..." << std::endl;
    stopWorker_ = true;
    if (worker_.joinable()) worker_.join();
    if (sock_ >= 0) close(sock_);
    std::cout << "[PhysicalLayer] Destructor finished." << std::endl;
}



void PhysicalLayerUdp::tick() {
    // Wysyłanie ramek z kolejki CodingModule
    while (!outgoingFramesFromCodingModule_.empty()) {
        Frame frame = outgoingFramesFromCodingModule_.front();
        outgoingFramesFromCodingModule_.pop();
        sendto(sock_, frame.data.data(), frame.data.size(), 0,
               (struct sockaddr*)&remoteAddr_, sizeof(remoteAddr_));
        std::cout << "[PhysicalLayer] Sent frame, size=" << frame.data.size() << std::endl;
    }
    // Odbiór ramek
    uint8_t buffer[FRAME_SIZE];
    sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);
    ssize_t received = recvfrom(sock_, buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&sender, &senderLen);
    while (received > 0) {
        Frame frame;
        frame.data.assign(buffer, buffer + received);
        unpadFrame(frame);
        std::cout << "[PhysicalLayer] Received frame, size=" << received << std::endl;
        codingModule_.receiveFrameWithCrc(frame);
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

void PhysicalLayerUdp::padFrame(Frame& frame) {
    if (frame.data.size() < FRAME_SIZE) {
        frame.data.resize(FRAME_SIZE, 0);
    }
}

void PhysicalLayerUdp::unpadFrame(Frame& frame) {
    // Padding usuwany tylko jeśli znasz oryginalny rozmiar (np. w polu ramki)
    // Tu zostawiamy ramkę jak jest, bo długość payloadu powinna być znana wyżej
}
