#include "CodingModule.hpp"
#include "TransportLayer.hpp"
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std;
using namespace chrono;

CodingModule::CodingModule(queue<Frame>& inputFrames, TransportLayer& transportLayer, const ValidationConfig& validationConfig)
	: LoggerBase("CodingModule"),
	  inputFrames_(inputFrames),
	  transportLayer_(transportLayer),
	  validationConfig_(validationConfig) {
	initializeConstraints();
	worker_ = thread([this]() {
		try {
			while (!stopWorker_) {
				while (!inputFrames_.empty()) {
					Frame frame = inputFrames_.front();
					inputFrames_.pop();
					ensureFrameEncodable(frame);
					uint32_t crc = crc32(frame.data);
					Frame frameWithCrc = frame;
					if (frameWithCrc.data.size() > maxFrameBytesWithoutCrc_) {
						throw runtime_error("Frame size exceeded after validation");
					}
					for (int i = 0; i < 4; ++i) {
						frameWithCrc.data.push_back((crc >> (8 * (3 - i))) & 0xFF);
					}
					if (frameWithCrc.data.size() > maxFrameBytesWithCrc_) {
						throw runtime_error("Frame with CRC exceeds allowed length");
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
	ensureFrameDecodable(frameWithCrc);
	if (frameWithCrc.data.size() < CRC_BYTES) {
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
	ensureFrameEncodable(decodedFrame);
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

void CodingModule::initializeConstraints() {
	auto bitsToBytes = [](uint8_t bits) -> uint8_t {
		uint8_t bytes = static_cast<uint8_t>((bits + 7) / 8);
		return bytes == 0 ? static_cast<uint8_t>(1) : bytes;
	};

	uint8_t packageIdBytes = bitsToBytes(validationConfig_.packageIdBitWidth());
	uint8_t messageIdBytes = bitsToBytes(validationConfig_.messageIdBitWidth());
	uint8_t connectionIdBytes = bitsToBytes(validationConfig_.connectionIdBitWidth());
	uint8_t fragmentIdBytes = bitsToBytes(validationConfig_.fragmentIdBitWidth());
	uint8_t fragmentsCountBytes = bitsToBytes(validationConfig_.fragmentsCountBitWidth());
	uint8_t priorityBytes = bitsToBytes(validationConfig_.priorityBitWidth());
	payloadLengthBytes_ = 2;

	headerBytesWithoutPayload_ = static_cast<size_t>(packageIdBytes) +
		static_cast<size_t>(messageIdBytes) +
		static_cast<size_t>(connectionIdBytes) +
		static_cast<size_t>(fragmentIdBytes) +
		static_cast<size_t>(fragmentsCountBytes) +
		static_cast<size_t>(priorityBytes) +
		static_cast<size_t>(payloadLengthBytes_) +
		2; // format + requireAck (1 byte each)

	if (headerBytesWithoutPayload_ == 0) {
		throw runtime_error("CodingModule header bytes calculation failed");
	}

	maxPayloadBytes_ = (1ULL << (payloadLengthBytes_ * 8)) - 1ULL;
	maxFrameBytesWithoutCrc_ = headerBytesWithoutPayload_ + maxPayloadBytes_;
	maxFrameBytesWithCrc_ = maxFrameBytesWithoutCrc_ + CRC_BYTES;
	if (maxFrameBytesWithoutCrc_ < headerBytesWithoutPayload_ ||
		maxFrameBytesWithCrc_ < maxFrameBytesWithoutCrc_) {
		throw runtime_error("CodingModule frame constraints overflow");
	}
}

void CodingModule::ensureFrameEncodable(const Frame& frame) const {
	if (frame.data.size() < headerBytesWithoutPayload_) {
		throw runtime_error("Frame shorter than transport header");
	}
	if (frame.data.size() > maxFrameBytesWithoutCrc_) {
		ostringstream oss;
		oss << "Frame size " << frame.data.size() << " exceeds limit " << maxFrameBytesWithoutCrc_;
		throw runtime_error(oss.str());
	}
}

void CodingModule::ensureFrameDecodable(const Frame& frameWithCrc) const {
	if (frameWithCrc.data.size() > maxFrameBytesWithCrc_) {
		ostringstream oss;
		oss << "Received frame size " << frameWithCrc.data.size()
		    << " exceeds limit " << maxFrameBytesWithCrc_;
		throw runtime_error(oss.str());
	}
}
