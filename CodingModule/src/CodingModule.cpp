#include "CodingModule.hpp"
#include "../transport_layer/include/transport_layer.hpp"
#include <thread>
#include <iostream>

CodingModule::CodingModule(std::queue<Frame>& inputFrames, TransportLayer& transportLayer)
	: inputFrames_(inputFrames), transportLayer_(transportLayer) {
	worker_ = std::thread([this]() {
		try {
			while (!stopWorker_) {
				while (!inputFrames_.empty()) {
					Frame frame = inputFrames_.front();
					inputFrames_.pop();
					uint32_t crc = crc32(frame.data);
					Frame frameWithCrc = frame;
					for (int i = 0; i < 4; ++i) {
						frameWithCrc.data.push_back((crc >> (8 * (3 - i))) & 0xFF);
					}
					outgoingFrames_.push(frameWithCrc);
					std::cout << "[CodingModule] Frame encoded (CRC32): size=" << frameWithCrc.data.size() << std::endl;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		} catch (const std::exception& ex) {
			std::cerr << "[CodingModule][EXCEPTION] " << ex.what() << std::endl;
		} catch (...) {
			std::cerr << "[CodingModule][EXCEPTION] unknown exception" << std::endl;
		}
	});
}

CodingModule::~CodingModule() {
	std::cout << "[CodingModule] Destructor called, stopping worker..." << std::endl;
	stopWorker_ = true;
	if (worker_.joinable()) worker_.join();
	std::cout << "[CodingModule] Destructor finished." << std::endl;
}

std::queue<Frame>& CodingModule::getOutgoingFrames() {
	return outgoingFrames_;
}

void CodingModule::receiveFrameWithCrc(const Frame& frameWithCrc) {
	if (frameWithCrc.data.size() < 4) throw std::runtime_error("Frame too short for CRC32");
	size_t n = frameWithCrc.data.size();
	std::vector<uint8_t> data(frameWithCrc.data.begin(), frameWithCrc.data.end() - 4);
	uint32_t receivedCrc = 0;
	for (int i = 0; i < 4; ++i) {
		receivedCrc = (receivedCrc << 8) | frameWithCrc.data[n - 4 + i];
	}
	uint32_t computedCrc = crc32(data);
	if (receivedCrc != computedCrc) {
		throw std::runtime_error("CRC32 mismatch: transmission error detected");
	}
	Frame decodedFrame;
	decodedFrame.data = std::move(data);
	transportLayer_.receiveFrame(decodedFrame);
	std::cout << "[CodingModule] Frame decoded and passed to TransportLayer (CRC OK)\n";
}

// Prosta implementacja CRC32 (standard polinom 0xEDB88320)
uint32_t CodingModule::crc32(const std::vector<uint8_t>& data) {
	uint32_t crc = 0xFFFFFFFF;
	for (uint8_t b : data) {
		crc ^= b;
		for (int i = 0; i < 8; ++i) {
			if (crc & 1)
				crc = (crc >> 1) ^ 0xEDB88320;
			else
				crc >>= 1;
		}
	}
	return ~crc;
}
