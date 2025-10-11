#include "EminentSdk.hpp"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std;

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    cout << "Starting EminentFeedSystem demo" << endl;

    EminentSdk sdkA(8001, "127.0.0.1", 8002);
    EminentSdk sdkB(8002, "127.0.0.1", 8001);
   

    DeviceId idA = 1001;
    DeviceId idB = 2002;
    bool aInitialized = false, bInitialized = false;
    ConnectionId finalConnA = -1;
    ConnectionId finalConnB = -1;


    sdkA.initialize(
        idA,
        [&]() {
            cout << "sdkA initialized!" << endl;
            aInitialized = true;
        },
        [&](const string& err) {
            cout << "sdkA init failed: " << err << endl;
        },
        [&](DeviceId remoteId, const string& payload) {
            ostringstream oss;
            oss << "sdkA: handshake from " << remoteId << ", payload='" << payload << "'";
            cout << oss.str() << endl;
            bool accept = payload != "ipockowanfwa";
            cout << "sdkA: handshake " << (accept ? "accepted" : "rejected") << endl;
            return accept;
        }
    );

    sdkB.initialize(
        idB,
        [&]() {
            cout << "sdkB initialized!" << endl;
            bInitialized = true;
        },
        [&](const string& err) {
            cout << "sdkB init failed: " << err << endl;
        },
        [&](DeviceId remoteId, const string& payload) {
            ostringstream oss;
            oss << "sdkB: handshake from " << remoteId << ", payload='" << payload << "'";
            cout << oss.str() << endl;
            bool accept = payload != "ipockowanfwa";
            cout << "sdkB: handshake " << (accept ? "accepted" : "rejected") << endl;
            return accept;
        },
        [idB, &finalConnB, &sdkB](ConnectionId connId, DeviceId remoteId) {
            finalConnB = connId;
            sdkB.setOnMessageHandler(connId, [remoteId](const Message& msg) {
                ostringstream handlerOss;
                handlerOss << "sdkB handler: message from " << remoteId
                           << " on conn " << msg.connId << ": " << msg.payload;
                cout << handlerOss.str() << endl;
            });
            ostringstream oss;
            oss << "SDK " << idB
                << " established a connection with device " << remoteId
                << ", connection id " << connId;
            cout << oss.str() << endl;
        }
    );

    while (!aInitialized || !bInitialized) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    auto onMessage = [&](const Message& msg) {
        ostringstream oss;
        oss << "Receiver (" << idB << ") got message: " << msg.payload;
        cout << oss.str() << endl;
    };

    sdkA.connect(
        idB,
        5,
        [&](ConnectionId cid) {
            cout << "Connect success, connection id: " << cid << endl;
        },
        [&](const string& err) {
            cout << "Connect failed: " << err << endl;
        },
        [&](const string& trouble) {
            cout << "Trouble: " << trouble << endl;
        },
        [&]() {
            cout << "Disconnected!" << endl;
        },
        [&](ConnectionId cid) {
            finalConnA = cid;
            sdkA.setOnMessageHandler(cid, [cid](const Message& msg) {
                ostringstream handlerOss;
                handlerOss << "sdkA handler: message on conn " << cid << ": " << msg.payload;
                cout << handlerOss.str() << endl;
            });
            cout << "sdkA onConnected: final connection id " << cid << endl;
        },
        onMessage
    );

    this_thread::sleep_for(chrono::milliseconds(2000));

    if (finalConnA != -1) {
    sdkA.setDefaultPriority(finalConnA, 6);
    cout << "sdkA: default priority set to 6" << endl;
    } else {
        cout << "Warning: sdkA final connection id not available, cannot set priority." << endl;
    }

    if (finalConnB != -1) {
    sdkB.setDefaultPriority(finalConnB, 4);
    cout << "sdkB: default priority set to 4" << endl;
    } else {
        cout << "Warning: sdkB final connection id not available, cannot set priority." << endl;
    }

    sdkA.complexConsoleInfo("SDK A");
    sdkB.complexConsoleInfo("SDK B");

    
    this_thread::sleep_for(chrono::milliseconds(500));

    if (finalConnA != -1) {
        try {
            sdkA.send(
                finalConnA,
                "{\"text\": \"Greetings from A to B\", \"from\": 1001}",
                MessageFormat::JSON,
                6,
                true,
                []() { cout << "sdkA: message delivered callback" << endl; }
            );
        } catch (const std::exception& ex) {
            cout << "sdkA: send failed - " << ex.what() << endl;
        }
    } else {
        cout << "Warning: sdkA cannot send message, connection id missing." << endl;
    }

    if (finalConnB != -1) {
        try {
            sdkB.send(
                finalConnB,
                "{\"text\": \"Greetings from B to A\", \"from\": 2002}",
                MessageFormat::JSON,
                4,
                true,
                []() { cout << "sdkB: message delivered callback" << endl; }
            );
        } catch (const std::exception& ex) {
            cout << "sdkB: send failed - " << ex.what() << endl;
        }
    } else {
        cout << "Warning: sdkB cannot send message, connection id missing." << endl;
    }

    this_thread::sleep_for(chrono::milliseconds(1000));

    cout << "Test finished." << endl;
    return 0;
}
