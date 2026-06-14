#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <commonTypes.hpp>
#include <logging.hpp>
#include <ValidationConfig.hpp>
#include "AbstractPhysicalLayer.hpp"
#include "SessionManager.hpp"
#include "TransportLayer.hpp"
#include "CodingModule.hpp"
#include "ICryptoModule.hpp"

#define EMINENT_SDK_VERSION_MAJOR 1
#define EMINENT_SDK_VERSION_MINOR 0
#define EMINENT_SDK_VERSION_PATCH 0
#define EMINENT_SDK_VERSION "1.0.0"

using namespace std;

class EminentSdk : public LoggerBase {
public:
    static const char* version() { return EMINENT_SDK_VERSION; }

    EminentSdk(unique_ptr<AbstractPhysicalLayer> physicalLayer,
               const ValidationConfig& validationConfig = ValidationConfig{},
               LogLevel logLevel = LogLevel::NONE);
    EminentSdk(int localPort, const string& remoteHost, int remotePort, LogLevel logLevel = LogLevel::NONE);
    EminentSdk(int localPort, const string& remoteHost, int remotePort, const ValidationConfig& validationConfig, LogLevel logLevel = LogLevel::NONE);
    ~EminentSdk();

    void initialize(
        DeviceId selfId,
        function<void()> onSuccess,
        function<void(const string&)> onFailure,
        function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision,
        function<void(ConnectionId, DeviceId)> onConnectionEstablished = nullptr
    );

    void connect(
        DeviceId targetId,
        Priority defaultPriority,
        function<void(ConnectionId)> onSuccess,
        function<void(const string&)> onFailure,
        function<void(const string&)> onTrouble,
        function<void()> onDisconnected,
        function<void(ConnectionId)> onConnected,
        function<void(const Message&)> onMessage,
        chrono::milliseconds heartbeatInterval = chrono::milliseconds{2000},
        function<void(ConnectionId)> onHeartbeatMissed = nullptr,
        chrono::milliseconds handshakeTimeout = chrono::milliseconds{10000}
    );

    // --- Disconnect ---
    void disconnect(ConnectionId id);
    void close(ConnectionId id); // legacy alias for disconnect

    // --- Send text data ---
    void send(
        ConnectionId id,
        const string& payload,
        MessageFormat format,
        Priority priority,
        bool requireAck,
        function<void()> onDelivered
    );

    // Simplified send (uses connection default priority, requireAck=true)
    void send(
        ConnectionId id,
        const string& payload,
        function<void()> onDelivered = nullptr
    );

    // --- Send binary data ---
    void sendBinary(
        ConnectionId id,
        const vector<uint8_t>& data,
        function<void()> onDelivered = nullptr
    );

    void sendBinary(
        ConnectionId id,
        const vector<uint8_t>& data,
        Priority priority,
        bool requireAck,
        function<void()> onDelivered
    );

    // --- Connection management ---
    void setOnMessageHandler(ConnectionId id, function<void(const Message&)> handler);
    void setOnDisconnected(ConnectionId id, function<void()> handler);
    void setDefaultPriority(ConnectionId id, Priority priority);
    vector<ConnectionId> getActiveConnectionIds() const;
    vector<DeviceId> getConnectedDeviceIds() const;

    // --- Retransmission configuration ---
    void setRetransmissionConfig(int maxAttempts, chrono::milliseconds interval);
    int getMaxRetransmitAttempts() const;
    chrono::milliseconds getRetransmitInterval() const;

    // --- Encryption ---
    void setCryptoModule(shared_ptr<ICryptoModule> cryptoModule);
    void addEncryptionKey(uint8_t keyId, const vector<uint8_t>& key);
    void removeEncryptionKey(uint8_t keyId);
    void setDefaultEncryptionKey(uint8_t keyId);
    void setConnectionEncryptionKey(ConnectionId connId, uint8_t keyId);
    void enableEncryption(bool enabled);
    bool isEncryptionEnabled() const;

    // --- Error callback (transport-level issues) ---
    void setOnTransportError(function<void(const string&)> handler);

    // --- Lifecycle ---
    void shutdown();

    // --- Info & Stats ---
    void onMessageReceived(const Message& msg);
    void complexConsoleInfo(const string& title = "");

    void getStats(
        function<void(const vector<ConnectionStats>&)> onStats,
        ConnectionId id = -1
    );

private:
    // Thread safety: protects connections_, heartbeats_, pendingHandshakes_
    mutable recursive_mutex mutex_;

    DeviceId deviceId_ = 0;
    ConnectionId nextConnectionId_ = 2;
    MessageId nextMsgId_ = 1;
    MessageId nextMessageId();
    ConnectionId nextPrime();
    int generateSpecialCode();

    bool initialized_ = false;
    atomic<bool> shutdownRequested_{false};
    function<bool(DeviceId, const string& payload)> onIncomingConnectionDecision_;
    function<void(ConnectionId, DeviceId)> onConnectionEstablished_;
    function<void(const string&)> onTransportError_;
    unordered_map<int, Connection> connections_;
    queue<Message> outgoingQueue_;

    // --- Handshake timeout ---
    struct PendingHandshake {
        ConnectionId initialCid;
        chrono::steady_clock::time_point deadline;
        function<void(const string&)> onFailure;
    };
    vector<PendingHandshake> pendingHandshakes_;
    void checkHandshakeTimeouts();

    ValidationConfig validationConfig_;
    SessionManager sessionManager_;
    TransportLayer transportLayer_;
    CodingModule codingModule_;
    unique_ptr<AbstractPhysicalLayer> physicalLayer_;
    int localPort_ = 0;
    string remoteHost_;
    int remotePort_ = 0;

    // --- Heartbeat state per connection ---
    struct HeartbeatState {
        chrono::milliseconds interval{2000};
        chrono::steady_clock::time_point lastSent{};
        chrono::steady_clock::time_point lastReceived{};
        bool waitingForResponse = false;
        function<void(ConnectionId)> onMissed;
    };
    unordered_map<ConnectionId, HeartbeatState> heartbeats_;
    thread heartbeatWorker_;
    atomic<bool> stopHeartbeat_{false};
    void heartbeatLoop();
    void sendHeartbeat(ConnectionId connId);
    void handleHeartbeat(const Message& msg);
    void handleHeartbeatAck(const Message& msg);

    // --- Disconnect protocol ---
    void sendDisconnectMessage(ConnectionId id);
    void handleDisconnectMessage(const Message& msg);

    // --- Handshake ---
    struct HandshakePayload {
        bool hasDeviceId = false;
        int deviceId = 0;
        bool hasSpecialCode = false;
        int specialCode = 0;
        bool hasNewId = false;
        int newId = 0;
        bool hasFinalConfirmation = false;
        bool finalConfirmation = false;
    };

    optional<HandshakePayload> parseHandshakePayload(const string& payload);
    void handleHandshakeRequest(const Message& msg, const HandshakePayload& payload);
    void handleHandshakeResponse(const Message& msg, const HandshakePayload& payload);
    void handleHandshakeFinalConfirmation(const Message& msg, const HandshakePayload& payload);
    void handleJsonMessage(const Message& msg);
    void handleVideoMessage(const Message& msg);
    Message decryptMessageIfNeeded(const Message& msg);
    string statusToString(ConnectionStatus status) const;

    // --- Encryption state ---
    shared_ptr<ICryptoModule> cryptoModule_;
    uint8_t defaultKeyId_ = 0;
    bool encryptionEnabled_ = false;
    unordered_map<ConnectionId, uint8_t> connectionKeyMap_;

    // --- Encryption helpers ---
    vector<uint8_t> encryptPayload(ConnectionId connId, const vector<uint8_t>& plaintext);
    vector<uint8_t> decryptPayload(const vector<uint8_t>& ciphertext);
    bool shouldEncrypt(MessageFormat format) const;
    uint8_t getKeyForConnection(ConnectionId connId) const;

    // --- Helpers ---
    unordered_map<int, Connection>::iterator findConnection(ConnectionId id);
};
