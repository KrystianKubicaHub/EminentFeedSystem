#pragma once
#include <queue>
#include <cstdint>
#include <common_types.hpp>
#include <thread>
#include <atomic>

class TransportLayer;

class CodingModule {
public:
	CodingModule(std::queue<Frame>& inputFrames, TransportLayer& transportLayer);
	~CodingModule();
	std::queue<Frame>& getOutgoingFrames();
	void receiveFrameWithCrc(const Frame& frameWithCrc);

private:
	uint32_t crc32(const std::vector<uint8_t>& data);
	std::queue<Frame>& inputFrames_;
	std::queue<Frame> outgoingFrames_;
	TransportLayer& transportLayer_;
	std::thread worker_;
	std::atomic<bool> stopWorker_{false};
};
