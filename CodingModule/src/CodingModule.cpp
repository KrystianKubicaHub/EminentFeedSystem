#include "CodingModule.hpp"
#include "../transport_layer/include/transport_layer.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std;
using namespace chrono;

CodingModule::CodingModule(queue<Frame>& inputFrames, TransportLayer& transportLayer)
	: LoggerBase("CodingModule"), inputFrames_(inputFrames), transportLayer_(transportLayer) {
	worker_ = thread([this]() {
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
					ostringstream oss;
					oss << "Frame encoded (CRC32) size=" << frameWithCrc.data.size();
					log(LogLevel::DEBUG, oss.str());
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

CodingModule::~CodingModule() {
	log(LogLevel::DEBUG, "Destructor invoked, signaling worker stop");
	stopWorker_ = true;
	if (worker_.joinable()) {
		worker_.join();
	}
	log(LogLevel::DEBUG, "Worker stopped");
}

queue<Frame>& CodingModule::getOutgoingFrames() {
	return outgoingFrames_;
}

void CodingModule::receiveFrameWithCrc(const Frame& frameWithCrc) {
	if (frameWithCrc.data.size() < 4) {
		log(LogLevel::ERROR, "Frame too short to contain CRC");
		throw runtime_error("Frame too short for CRC32");
	}
	size_t n = frameWithCrc.data.size();
	vector<uint8_t> data(frameWithCrc.data.begin(), frameWithCrc.data.end() - 4);
	uint32_t receivedCrc = 0;
	for (int i = 0; i < 4; ++i) {
		receivedCrc = (receivedCrc << 8) | frameWithCrc.data[n - 4 + i];
	}
	uint32_t computedCrc = crc32(data);
	if (receivedCrc != computedCrc) {
		log(LogLevel::ERROR, "CRC32 mismatch detected");
		throw runtime_error("CRC32 mismatch: transmission error detected");
	}
	Frame decodedFrame;
	decodedFrame.data = move(data);
	transportLayer_.receiveFrame(decodedFrame);
	log(LogLevel::DEBUG, "Frame decoded and forwarded to TransportLayer");
}

uint32_t CodingModule::crc32(const vector<uint8_t>& data) {
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
