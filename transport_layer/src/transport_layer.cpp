#include "transport_layer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

TransportLayer::TransportLayer(std::queue<Package>& outgoingPackages)
    : outgoingPackages_(outgoingPackages) {
    std::thread([this]() {
        while (true) {
            while (!outgoingPackages_.empty()) {
                Package pkg = outgoingPackages_.front();
                outgoingPackages_.pop();
                std::cout << "[TransportLayer] Package: id=" << pkg.packageId
                          << ", msgId=" << pkg.messageId
                          << ", frag=" << pkg.fragmentId << "/" << pkg.fragmentsCount
                          << ", payload='" << pkg.payload << "'\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}

