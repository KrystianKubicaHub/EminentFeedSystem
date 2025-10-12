#include "PhysicalLayerInMemory.hpp"

#include "CodingModule.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace std;
using namespace chrono_literals;

PhysicalLayerInMemory::PhysicalLayerInMemory(DeviceId selfId,
                                             shared_ptr<InMemoryMedium> medium)
    : AbstractPhysicalLayer("PhysicalLayerInMemory")
    , selfId_(selfId)
    , medium_(std::move(medium)) {
    if (!medium_) {
        throw invalid_argument("PhysicalLayerInMemory requires a shared medium");
    }
    registerParticipant();
}

PhysicalLayerInMemory::~PhysicalLayerInMemory() {
    stopWorker_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    unregisterParticipant();
}

void PhysicalLayerInMemory::configure(queue<Frame>& outgoingFramesFromCodingModule,
                                      CodingModule& codingModule,
                                      const ValidationConfig& validationConfig) {
    setEnvironment(outgoingFramesFromCodingModule, codingModule, validationConfig);
    stopWorker_ = false;
}

void PhysicalLayerInMemory::start() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerInMemory cannot start before configuration");
    }
    if (worker_.joinable()) {
        return;
    }
    worker_ = thread([this]() { workerLoop(); });
}

void PhysicalLayerInMemory::tick() {
    if (!isConfigured()) {
        throw runtime_error("PhysicalLayerInMemory tick called before configuration");
    }
    processOutgoingFrames();
    processIncomingFrames();
}

bool PhysicalLayerInMemory::tryReceive(Frame& outFrame) {
    if (incomingFrames_.empty()) {
        return false;
    }
    outFrame = incomingFrames_.front();
    incomingFrames_.pop();
    return true;
}

void PhysicalLayerInMemory::registerParticipant() {
    lock_guard<mutex> lock(medium_->mutex);
    auto [_, inserted] = medium_->participants.insert(selfId_);
    if (!inserted) {
        throw runtime_error("Participant with this DeviceId already registered in medium");
    }
}

void PhysicalLayerInMemory::unregisterParticipant() {
    lock_guard<mutex> lock(medium_->mutex);
    medium_->participants.erase(selfId_);
    for (auto it = medium_->entries.begin(); it != medium_->entries.end();) {
        if (it->senderId == selfId_) {
            it = medium_->entries.erase(it);
            continue;
        }
        it->deliveredTo.erase(selfId_);
        ++it;
    }
}

void PhysicalLayerInMemory::processOutgoingFrames() {
    while (outgoingFramesFromCodingModule_ && !outgoingFramesFromCodingModule_->empty()) {
        Frame frame = outgoingFramesFromCodingModule_->front();
        outgoingFramesFromCodingModule_->pop();
        ensureEncodableFrame(frame);

        lock_guard<mutex> lock(medium_->mutex);
        medium_->entries.push_back({selfId_, frame, {}});
    }
}

void PhysicalLayerInMemory::processIncomingFrames() {
    vector<Frame> framesToDeliver;
    {
        lock_guard<mutex> lock(medium_->mutex);
        auto participantsCount = medium_->participants.size();
        for (auto it = medium_->entries.begin(); it != medium_->entries.end();) {
            auto& entry = *it;
            if (entry.senderId == selfId_) {
                ++it;
                continue;
            }

            if (entry.deliveredTo.insert(selfId_).second) {
                framesToDeliver.push_back(entry.frame);
            }

            size_t receiversNeeded = participantsCount > 0 ? participantsCount - 1 : 0;
            if (entry.deliveredTo.size() >= receiversNeeded) {
                it = medium_->entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& frame : framesToDeliver) {
        ensureDecodableFrame(frame);
        incomingFrames_.push(frame);
        if (codingModule_) {
            codingModule_->receiveFrameWithCrc(frame);
        }
    }
}

void PhysicalLayerInMemory::workerLoop() {
    try {
        while (!stopWorker_) {
            processOutgoingFrames();
            processIncomingFrames();
            this_thread::sleep_for(5ms);
        }
    } catch (const exception& ex) {
        log(LogLevel::ERROR, string("InMemory worker exception: ") + ex.what());
    } catch (...) {
        log(LogLevel::ERROR, "InMemory worker exception: unknown");
    }
}
