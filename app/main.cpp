#include "eminent_sdk.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

int main() {
    EminentSdk sdkA(8001, "127.0.0.1", 8002);
    EminentSdk sdkB(8002, "127.0.0.1", 8001);
   

    DeviceId idA = 1001;
    DeviceId idB = 2002;
    bool aInitialized = false, bInitialized = false;


    sdkA.initialize(
        idA,
        [&]() { cout << "sdkA initialized!\n"; aInitialized = true; },
        [&](const string& err) { cout << "sdkA init failed: " << err << endl; },
        [](DeviceId remoteId, const std::string& payload) {
            cout << "sdkA: handshake from " << remoteId << ", payload='" << payload << "'\n";
            // Akceptujemy tylko jeśli payload zawiera "ipockowanfwa"
            bool accept = payload != "ipockowanfwa";
            cout << (accept ? "sdkA: handshake accepted\n" : "sdkA: handshake rejected\n");
            return accept;
        }
    );

    sdkB.initialize(
        idB,
        [&]() { cout << "sdkB initialized!\n"; bInitialized = true; },
        [&](const string& err) { cout << "sdkB init failed: " << err << endl; },
        [](DeviceId remoteId, const std::string& payload) {
            cout << "sdkB: handshake from " << remoteId << ", payload='" << payload << "'\n";
            // Akceptujemy tylko jeśli payload zawiera "ipockowanfwa"
            bool accept = payload != "ipockowanfwa";
            cout << (accept ? "sdkB: handshake accepted\n" : "sdkB: handshake rejected\n");
            return accept;
        },
        [idB](ConnectionId connId, DeviceId remoteId) {
            cout << "Radośnie informuję, że SDK o id " << idB
                 << " połączył się z userem o id " << remoteId
                 << ", połączenie ma id " << connId << endl;
        }
    );

    while (!aInitialized || !bInitialized) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    auto onMessage = [&](const Message& msg) {
        cout << "Receiver (" << idB << ") got message: " << msg.payload << endl;
    };

    sdkA.connect(
        idB,
        5,
        [&](ConnectionId cid) { cout << "Connect success, connection id: " << cid << endl; },
        [&](const string& err) { cout << "Connect failed: " << err << endl; },
        [&](const string& trouble) { cout << "Trouble: " << trouble << endl; },
        [&]() { cout << "Disconnected!" << endl; },
        [&](ConnectionId) {}, // onConnected
        onMessage
    );

    this_thread::sleep_for(chrono::milliseconds(100));

    cout << "Test finished." << endl;
    return 0;
}
