#include "PhysicalLayerUdp.hpp"
#include "CodingModule.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

using namespace std;
using namespace chrono;

PhysicalLayerUdp::PhysicalLayerUdp(int localPort,
                                   const string& remoteHost,
                                   int remotePort)
    : AbstractPhysicalLayer("PhysicalLayerUdp")
    , remotePort_(remotePort)
    , localPort_(localPort)
    , remoteHost_(remoteHost) {

    if (localPort <= 0 || localPort > 65535) {
        throw invalid_argument("PhysicalLayerUdp: localPort must be in range 1-65535, got " + to_string(localPort));
    }
    if (remotePort <= 0 || remotePort > 65535) {
        throw invalid_argument("PhysicalLayerUdp: remotePort must be in range 1-65535, got " + to_string(remotePort));
    }
    if (remoteHost.empty()) {
        throw invalid_argument("PhysicalLayerUdp: remoteHost must not be empty");
    }

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw runtime_error(string("PhysicalLayerUdp: failed to create socket: ") + strerror(errno));
    }

    memset(&localAddr_, 0, sizeof(localAddr_));
    localAddr_.sin_family = AF_INET;
    localAddr_.sin_addr.s_addr = INADDR_ANY;
    localAddr_.sin_port = htons(localPort);
    if (::bind(sock_, reinterpret_cast<struct sockaddr*>(&localAddr_), sizeof(localAddr_)) < 0) {
        string err = strerror(errno);
        ::close(sock_);
        throw runtime_error(string("PhysicalLayerUdp: failed to bind to port ") +
            to_string(localPort) + ": " + err);
    }

    memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort);
    if (inet_pton(AF_INET, remoteHost.c_str(), &remoteAddr_.sin_addr) != 1) {
        ::close(sock_);
        throw runtime_error(string("PhysicalLayerUdp: invalid remote address '") + remoteHost + "'");
    }

    fcntl(sock_, F_SETFL, O_NONBLOCK);
    log(LogLevel::INFO, string("UDP socket bound to port ") + to_string(localPort) +
        ", remote=" + remoteHost + ":" + to_string(remotePort));
}

void PhysicalLayerUdp::configure(ThreadSafeQueue<Frame>& outgoingFramesFromCodingModule,
                                 CodingModule& codingModule,
                                 const ValidationConfig& validationConfig) {
    setEnvironment(outgoingFramesFromCodingModule, codingModule, validationConfig);
    recvBuffer_.resize(maxFrameBytesWithCrc());
    stopWorker_ = false;
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

void PhysicalLayerUdp::start() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerUdp cannot start before configuration");
    }
    if (worker_.joinable()) {
        return;
    }
    worker_ = thread([this]() { workerLoop(); });
}

void PhysicalLayerUdp::workerLoop() {
    int consecutiveErrors = 0;
    static constexpr int MAX_CONSECUTIVE_ERRORS = 50;

    try {
        while (!stopWorker_) {
            Frame frame;
            while (outgoingFramesFromCodingModule_ && outgoingFramesFromCodingModule_->tryPop(frame)) {
                ensureEncodableFrame(frame);
                ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                                       reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
                if (sent < 0) {
                    int err = errno;
                    string errMsg = string("UDP send failed: ") + strerror(err) +
                        " (errno=" + to_string(err) + ", frameSize=" + to_string(frame.data.size()) + ")";

                    if (err == ENETUNREACH || err == EHOSTUNREACH) {
                        log(LogLevel::ERROR, errMsg + " [network unreachable]");
                    } else if (err == EMSGSIZE) {
                        log(LogLevel::ERROR, errMsg + " [message too large for MTU]");
                    } else if (err == ENOBUFS || err == ENOMEM) {
                        log(LogLevel::WARN, errMsg + " [buffer full, will retry]");
                        // Re-queue the frame for retry
                        if (outgoingFramesFromCodingModule_) {
                            outgoingFramesFromCodingModule_->push(frame);
                        }
                        this_thread::sleep_for(5ms);
                    } else {
                        log(LogLevel::ERROR, errMsg);
                    }

                    consecutiveErrors++;
                    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                        log(LogLevel::ERROR, "Too many consecutive send errors (" +
                            to_string(consecutiveErrors) + "), pausing worker for 1s");
                        this_thread::sleep_for(1s);
                        consecutiveErrors = 0;
                    }
                    continue;
                }
                consecutiveErrors = 0;
                log(LogLevel::DEBUG, string("Sent frame size=") + to_string(frame.data.size()));
            }

            sockaddr_in sender{};
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
                    log(LogLevel::WARN, string("Dropping invalid received frame: ") + ex.what() +
                        " (size=" + to_string(received) + ")");
                }
                received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                                    reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
            }

            // Check for recv errors (other than EAGAIN/EWOULDBLOCK which is normal for non-blocking)
            if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                log(LogLevel::WARN, string("UDP recv error: ") + strerror(errno));
            }

            this_thread::sleep_for(10ms);
        }
    } catch (const exception& ex) {
        log(LogLevel::ERROR, string("Worker fatal exception: ") + ex.what());
    } catch (...) {
        log(LogLevel::ERROR, "Worker fatal exception: unknown");
    }
}

void PhysicalLayerUdp::tick() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerUdp tick called before configuration");
    }

    Frame frame;
    while (outgoingFramesFromCodingModule_->tryPop(frame)) {
        ensureEncodableFrame(frame);
        ssize_t sent = sendto(sock_, frame.data.data(), frame.data.size(), 0,
                               reinterpret_cast<struct sockaddr*>(&remoteAddr_), sizeof(remoteAddr_));
        if (sent < 0) {
            int err = errno;
            log(LogLevel::ERROR, string("Tick send failed: ") + strerror(err) +
                " (errno=" + to_string(err) + ", size=" + to_string(frame.data.size()) + ")");
        } else {
            log(LogLevel::DEBUG, string("Tick sent frame size=") + to_string(frame.data.size()));
        }
    }

    sockaddr_in sender{};
    socklen_t senderLen = sizeof(sender);
    ssize_t received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
                                reinterpret_cast<struct sockaddr*>(&sender), &senderLen);
    while (received > 0) {
        Frame rxFrame;
        rxFrame.data.assign(recvBuffer_.begin(), recvBuffer_.begin() + received);
        try {
            ensureDecodableFrame(rxFrame);
            log(LogLevel::DEBUG, string("Tick received frame size=") + to_string(received));
            codingModule_->receiveFrameWithCrc(rxFrame);
        } catch (const exception& ex) {
            log(LogLevel::WARN, string("Tick: dropping invalid frame: ") + ex.what());
        }
        received = recvfrom(sock_, recvBuffer_.data(), recvBuffer_.size(), 0,
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
