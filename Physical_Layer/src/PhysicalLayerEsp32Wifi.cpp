#ifdef ESP_PLATFORM

#include "PhysicalLayerEsp32Wifi.hpp"
#include "CodingModule.hpp"

#include <cstring>
#include <chrono>
#include <stdexcept>

// ESP-IDF logging
#include <esp_log.h>

static const char* TAG = "PhysLayerESP32";

using namespace std;
using namespace chrono;

PhysicalLayerEsp32Wifi::PhysicalLayerEsp32Wifi(int localPort,
                                               const string& remoteHost,
                                               int remotePort)
    : AbstractPhysicalLayer("PhysicalLayerEsp32Wifi")
    , localPort_(localPort)
    , remotePort_(remotePort)
    , remoteHost_(remoteHost) {

    if (localPort <= 0 || localPort > 65535) {
        throw invalid_argument("PhysicalLayerEsp32Wifi: localPort must be 1-65535, got " + to_string(localPort));
    }
    if (remotePort <= 0 || remotePort > 65535) {
        throw invalid_argument("PhysicalLayerEsp32Wifi: remotePort must be 1-65535, got " + to_string(remotePort));
    }
    if (remoteHost.empty()) {
        throw invalid_argument("PhysicalLayerEsp32Wifi: remoteHost must not be empty");
    }

    // Create UDP socket
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        throw runtime_error("PhysicalLayerEsp32Wifi: failed to create socket");
    }

    // Bind to local port
    memset(&localAddr_, 0, sizeof(localAddr_));
    localAddr_.sin_family = AF_INET;
    localAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr_.sin_port = htons(localPort);

    if (bind(sock_, reinterpret_cast<struct sockaddr*>(&localAddr_), sizeof(localAddr_)) < 0) {
        ESP_LOGE(TAG, "Failed to bind to port %d: errno %d", localPort, errno);
        close(sock_);
        throw runtime_error("PhysicalLayerEsp32Wifi: failed to bind to port " + to_string(localPort));
    }

    // Set non-blocking
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

    // Setup remote address
    memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteHost.c_str(), &remoteAddr_.sin_addr);

    ESP_LOGI(TAG, "UDP socket bound to port %d, remote=%s:%d", localPort, remoteHost.c_str(), remotePort);
    log(LogLevel::INFO, string("ESP32 WiFi UDP bound to port ") + to_string(localPort) +
        ", remote=" + remoteHost + ":" + to_string(remotePort));
}

void PhysicalLayerEsp32Wifi::configure(ThreadSafeQueue<Frame>& outgoingFramesFromCodingModule,
                                       CodingModule& codingModule,
                                       const ValidationConfig& validationConfig) {
    setEnvironment(outgoingFramesFromCodingModule, codingModule, validationConfig);
    recvBuffer_.resize(maxFrameBytesWithCrc());
    stopWorker_ = false;
}

PhysicalLayerEsp32Wifi::~PhysicalLayerEsp32Wifi() {
    stopWorker_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    ESP_LOGI(TAG, "PhysicalLayerEsp32Wifi destroyed");
}

void PhysicalLayerEsp32Wifi::start() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerEsp32Wifi: cannot start before configuration");
    }
    if (worker_.joinable()) {
        return;
    }
    worker_ = thread([this]() { workerLoop(); });
}

void PhysicalLayerEsp32Wifi::workerLoop() {
    ESP_LOGI(TAG, "Worker loop started");

    while (!stopWorker_) {
        // --- Send outgoing frames ---
        Frame frame;
        while (outgoingFramesFromCodingModule_ && outgoingFramesFromCodingModule_->tryPop(frame)) {
            ensureEncodableFrame(frame);
            ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                                  reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
            if (sent < 0) {
                ESP_LOGW(TAG, "Send failed: errno %d, frame size %d", errno, (int)frame.data.size());
                log(LogLevel::WARN, string("ESP32 send failed: errno=") + to_string(errno));
            } else {
                log(LogLevel::DEBUG, string("Sent frame size=") + to_string(frame.data.size()));
            }
        }

        // --- Receive incoming frames ---
        struct sockaddr_in sender{};
        socklen_t senderLen = sizeof(sender);
        ssize_t received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                                    reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
        while (received > 0) {
            Frame rxFrame;
            rxFrame.data.assign(recvBuffer_.begin(), recvBuffer_.begin() + received);
            try {
                ensureDecodableFrame(rxFrame);
                log(LogLevel::DEBUG, string("Received frame size=") + to_string(received));
                if (codingModule_) {
                    codingModule_->receiveFrameWithCrc(rxFrame);
                }
            } catch (const exception& ex) {
                ESP_LOGW(TAG, "Dropping invalid frame: %s (size=%d)", ex.what(), (int)received);
                log(LogLevel::WARN, string("Dropping invalid frame: ") + ex.what());
            }
            received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                                reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
        }

        // Small delay to yield CPU on ESP32 (FreeRTOS friendly)
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGI(TAG, "Worker loop stopped");
}

void PhysicalLayerEsp32Wifi::tick() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerEsp32Wifi: tick called before configuration");
    }

    Frame frame;
    while (outgoingFramesFromCodingModule_->tryPop(frame)) {
        ensureEncodableFrame(frame);
        ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                              reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
        if (sent < 0) {
            log(LogLevel::ERROR, string("Tick send failed: errno=") + to_string(errno));
        }
    }

    struct sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);
    ssize_t received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                                reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
    while (received > 0) {
        Frame rxFrame;
        rxFrame.data.assign(recvBuffer_.begin(), recvBuffer_.begin() + received);
        try {
            ensureDecodableFrame(rxFrame);
            if (codingModule_) {
                codingModule_->receiveFrameWithCrc(rxFrame);
            }
        } catch (const exception& ex) {
            log(LogLevel::WARN, string("Tick: dropping invalid frame: ") + ex.what());
        }
        received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                            reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
    }
}

bool PhysicalLayerEsp32Wifi::tryReceive(Frame& outFrame) {
    if (incomingFrames_.empty()) {
        return false;
    }
    outFrame = incomingFrames_.front();
    incomingFrames_.pop();
    return true;
}

#endif // ESP_PLATFORM
