#include "session_manager.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <thread>

using namespace std;
using namespace std::chrono;


SessionManager::SessionManager(std::queue<Message>& sdkQueue, OnMessageCallback onMessage, size_t maxPacketSize)
    : sdkQueue_(sdkQueue), maxPacketSize_(maxPacketSize), onMessage_(onMessage) {
    worker_ = std::thread([this]() {
        while (!stopWorker_) {
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                std::queue<Message> tmp = sdkQueue_;
                std::vector<Message> toProcess;
                while (!tmp.empty()) {
                    const Message& msg = tmp.front();
                    if (!messageStatus_[msg.id]) {
                        toProcess.push_back(msg);
                    }
                    tmp.pop();
                }
                for (const Message& msg : toProcess) {
                    int total = (msg.payload.size() + maxPacketSize_ - 1) / maxPacketSize_;
                    for (int frag = 0; frag < total; ++frag) {
                        std::string fragment = msg.payload.substr(frag * maxPacketSize_, maxPacketSize_);
                        Package pkg{
                            nextPackageId_++,
                            msg.id,
                            msg.connId,
                            frag,
                            total,
                            fragment,
                            msg.format,
                            msg.priority,
                            msg.requireAck,
                            PackageStatus::QUEUED
                        };
                        outgoingPackages_.push(pkg);
                    }
                    messageStatus_[msg.id] = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

SessionManager::~SessionManager() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopWorker_ = true;
    }
    if (worker_.joinable()) worker_.join();
}

void SessionManager::processMessages() {
    std::queue<Message> tmp = sdkQueue_;
    std::vector<Message> toProcess;
    while (!tmp.empty()) {
        const Message& msg = tmp.front();
        if (!messageStatus_[msg.id]) {
            toProcess.push_back(msg);
        }
        tmp.pop();
    }
    for (const Message& msg : toProcess) {
        int total = (msg.payload.size() + maxPacketSize_ - 1) / maxPacketSize_;
        for (int frag = 0; frag < total; ++frag) {
            std::string fragment = msg.payload.substr(frag * maxPacketSize_, maxPacketSize_);
            Package pkg{
                nextPackageId_++,
                msg.id,
                msg.connId,
                frag,
                total,
                fragment,
                msg.format,
                msg.priority,
                msg.requireAck,
                PackageStatus::QUEUED
            };
            outgoingPackages_.push(pkg);
        }
        messageStatus_[msg.id] = true;
    }
}

void SessionManager::receivePackage(const Package& pkg) {

    receivedPackages_[pkg.messageId].push_back(pkg);
    auto& vec = receivedPackages_[pkg.messageId];

    if ((int)vec.size() < pkg.fragmentsCount) return;

    std::sort(vec.begin(), vec.end(), [](const Package& a, const Package& b) { return a.fragmentId < b.fragmentId; });
    std::string fullPayload;

    for (int i = 0; i < pkg.fragmentsCount; ++i) {
        if (vec[i].fragmentId != i) return;
        fullPayload += vec[i].payload;
    }

    Message msg{
        pkg.messageId,
        pkg.connId,
        fullPayload,
        pkg.format,
        pkg.priority,
        pkg.requireAck,
        nullptr
    };
    
    if (onMessage_) onMessage_(msg);
    
    receivedPackages_.erase(pkg.messageId);
}



