#include "eminent_sdk.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

using namespace std;

int main() {
    EminentSdk sdkA(8001, "127.0.0.1", 8002);
    EminentSdk sdkB(8002, "127.0.0.1", 8001);
   

    DeviceId idA = 1001;
    DeviceId idB = 2002;
    bool aInitialized = false, bInitialized = false;
    ConnectionId finalConnA = -1;
    ConnectionId finalConnB = -1;


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
        [idB, &finalConnB, &sdkB](ConnectionId connId, DeviceId remoteId) {
            finalConnB = connId;
            sdkB.setOnMessageHandler(connId, [remoteId](const Message& msg) {
                cout << "sdkB handler: message from " << remoteId
                     << " on conn " << msg.connId << ": " << msg.payload << endl;
            });
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
        [&](ConnectionId cid) {
            finalConnA = cid;
            sdkA.setOnMessageHandler(cid, [cid](const Message& msg) {
                cout << "sdkA handler: message on conn " << cid << ": " << msg.payload << endl;
            });
            cout << "sdkA onConnected: final connection id " << cid << endl;
        },
        onMessage
    );

    this_thread::sleep_for(chrono::milliseconds(2000));

    if (finalConnA != -1) {
        sdkA.setDefaultPriority(finalConnA, 6);
    } else {
        cout << "Warning: sdkA final connection id not available, cannot set priority." << endl;
    }

    if (finalConnB != -1) {
        sdkB.setDefaultPriority(finalConnB, 4);
    } else {
        cout << "Warning: sdkB final connection id not available, cannot set priority." << endl;
    }

    sdkA.complexConsoleInfo("SDK A");
    sdkB.complexConsoleInfo("SDK B");

    /// teraz tutaj chcieli byśmy dodać wysłanie wiadomości z A do B
    /// oraz z B do A
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (finalConnA != -1) {
        try {
            sdkA.send(
                finalConnA,
                "{\"text\": \"Pozdrowienia od A dla B\", \"from\": 1001}",
                MessageFormat::JSON,
                6,
                false,
                []() { std::cout << "sdkA: message delivered callback" << std::endl; }
            );
        } catch (const std::exception& ex) {
            std::cout << "sdkA: send failed - " << ex.what() << std::endl;
        }
    } else {
        std::cout << "Warning: sdkA cannot send message, connection id missing." << std::endl;
    }

    if (finalConnB != -1) {
        try {
            sdkB.send(
                finalConnB,
                "{\"text\": \"Pozdrowienia od B dla A\", \"from\": 2002}",
                MessageFormat::JSON,
                4,
                false,
                []() { std::cout << "sdkB: message delivered callback" << std::endl; }
            );
        } catch (const std::exception& ex) {
            std::cout << "sdkB: send failed - " << ex.what() << std::endl;
        }
    } else {
        std::cout << "Warning: sdkB cannot send message, connection id missing." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    cout << "Test finished." << endl;
    return 0;
}
