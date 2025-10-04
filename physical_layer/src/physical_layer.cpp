#include "physical_layer.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>


PhysicalLayerUdp::PhysicalLayerUdp(int localPort, const std::string& remoteHost, int remotePort)
    : remotePort_(remotePort), remoteHost_(remoteHost) {
    // Konstruktor - na razie nic nie robi
}

PhysicalLayerUdp::~PhysicalLayerUdp() {
    // Destruktor - na razie nic nie robi
}

void PhysicalLayerUdp::enqueueFrame(const Frame& frame) {
    // Nic nie robi
}

void PhysicalLayerUdp::tick() {
    // Nic nie robi
}

bool PhysicalLayerUdp::tryReceive(Frame& outFrame) {
    // Nic nie robi, zawsze zwraca false
    return false;
}
