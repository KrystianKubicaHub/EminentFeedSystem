# EminentFeedSystem SDK

> **Version 1.0.0** — A lightweight, real-time peer-to-peer communication SDK for embedded systems and drones.

## Features

- **Peer-to-peer connections** with handshake, heartbeat, and disconnect protocol
- **Multiple message formats** — JSON text and binary (VIDEO) payloads
- **Priority-based messaging** with configurable retransmission
- **Encryption** — Swappable ChaCha20 stream cipher (pre-shared keys, per-message nonce)
- **Transport-agnostic** — In-memory (testing) or UDP physical layer
- **Thread-safe** — Recursive mutex protection, safe for multi-threaded use
- **Zero external dependencies** — Pure C++17, no OpenSSL or Boost required

## Requirements

- C++17 compiler (GCC 8+, Clang 10+, AppleClang 12+, MSVC 2017+)
- CMake 3.10+
- Google Test v1.14.0 (auto-fetched via CMake FetchContent)

## Quick Start

### Integration via `add_subdirectory`

```cmake
# In your project's CMakeLists.txt:
add_subdirectory(path/to/EminentFeedSystem)
target_link_libraries(your_app
    eminent_sdk
    session_manager
    transport_layer
    physical_layer
    CodingModule
    common_utils
    crypto_module
)
```

### Build from source

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Run tests
ctest --output-on-failure
```

## Usage Example

```cpp
#include "EminentSdk.hpp"
#include "ChaCha20CryptoModule.hpp"

int main() {
    // Create SDK with UDP transport
    EminentSdk sdk(5000, "192.168.1.100", 5001, LogLevel::INFO);

    // Setup encryption (optional)
    auto crypto = std::make_shared<ChaCha20CryptoModule>();
    std::vector<uint8_t> sharedKey(32); // 256-bit pre-shared key
    // ... fill sharedKey with your secret ...
    crypto->addKey(1, sharedKey);

    sdk.setCryptoModule(crypto);
    sdk.addEncryptionKey(1, sharedKey);
    sdk.setDefaultEncryptionKey(1);
    sdk.enableEncryption(true);

    // Initialize
    sdk.initialize(
        /*selfId=*/ 1,
        /*onSuccess=*/ []() { printf("Initialized!\n"); },
        /*onFailure=*/ [](const std::string& err) { printf("Failed: %s\n", err.c_str()); },
        /*onIncomingConnectionDecision=*/ [](DeviceId id, const std::string&) { return true; },
        /*onConnectionEstablished=*/ [](ConnectionId cid, DeviceId did) {
            printf("Connection %d established with device %d\n", cid, did);
        }
    );

    // Connect to remote device
    sdk.connect(
        /*targetId=*/ 2,
        /*defaultPriority=*/ 5,
        /*onSuccess=*/ [](ConnectionId cid) { printf("Connected: %d\n", cid); },
        /*onFailure=*/ [](const std::string& err) { printf("Error: %s\n", err.c_str()); },
        /*onTrouble=*/ [](const std::string& msg) { printf("Warning: %s\n", msg.c_str()); },
        /*onDisconnected=*/ []() { printf("Disconnected\n"); },
        /*onConnected=*/ [](ConnectionId cid) { printf("Active: %d\n", cid); },
        /*onMessage=*/ [](const Message& msg) {
            printf("Received: %s\n", msg.payload.c_str());
        }
    );

    // Send encrypted message (encryption happens transparently)
    // sdk.send(connectionId, "{\"text\":\"Hello drone!\"}");

    // Send binary data
    // std::vector<uint8_t> frame = ...;
    // sdk.sendBinary(connectionId, frame);

    // Cleanup
    // sdk.shutdown();
    return 0;
}
```

## Architecture

```
┌─────────────────────────────────────────────┐
│               EminentSdk                     │  ← User API
├─────────────────────────────────────────────┤
│  Crypto Module (ChaCha20 / Null / Custom)   │  ← Encryption layer
├─────────────────────────────────────────────┤
│           Session Manager                    │  ← Connection lifecycle
├─────────────────────────────────────────────┤
│           Transport Layer                    │  ← Retransmission, ACK
├─────────────────────────────────────────────┤
│            Coding Module                     │  ← Encoding/framing
├─────────────────────────────────────────────┤
│   Physical Layer (UDP / InMemory)           │  ← Network I/O
└─────────────────────────────────────────────┘
```

## Encryption

The SDK uses a **pre-shared key** model ideal for drone-pilot scenarios:

- **Algorithm**: ChaCha20 stream cipher (RFC 7539 core)
- **Key size**: 256 bits (32 bytes)
- **Nonce**: 96 bits (12 bytes), randomly generated per message
- **Wire format**: `[keyId: 1B][nonce: 12B][ciphertext: NB]`
- **Scope**: Only user payloads (JSON, VIDEO) are encrypted; protocol messages (HANDSHAKE, HEARTBEAT, DISCONNECT, ACK) are not encrypted

### Encryption Setup

```cpp
#include "ChaCha20CryptoModule.hpp"

// Create and configure
auto crypto = std::make_shared<ChaCha20CryptoModule>();
std::vector<uint8_t> key(32, 0x42); // Your 256-bit key
crypto->addKey(1, key);

// Attach to SDK
sdk.setCryptoModule(crypto);
sdk.addEncryptionKey(1, key);
sdk.setDefaultEncryptionKey(1);
sdk.enableEncryption(true);

// Per-connection key override (optional)
sdk.setConnectionEncryptionKey(connectionId, 2);
```

### Custom Crypto Module

Implement `ICryptoModule` to use AES, hardware crypto, or any other algorithm:

```cpp
#include "ICryptoModule.hpp"

class MyHardwareCrypto : public ICryptoModule {
public:
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted) override;
    void addKey(uint8_t keyId, const std::vector<uint8_t>& key) override;
    void removeKey(uint8_t keyId) override;
    bool hasKey(uint8_t keyId) const override;
};
```

## API Reference

### Initialization

| Method | Description |
|--------|-------------|
| `EminentSdk(port, host, remotePort, logLevel)` | Create with UDP transport |
| `EminentSdk(unique_ptr<AbstractPhysicalLayer>, config, logLevel)` | Create with custom transport |
| `initialize(selfId, onSuccess, onFailure, onIncoming, onEstablished)` | Start SDK |
| `shutdown()` | Stop all connections and threads |

### Connection

| Method | Description |
|--------|-------------|
| `connect(targetId, priority, callbacks...)` | Initiate connection |
| `disconnect(id)` | Gracefully close connection |
| `getActiveConnectionIds()` | List active connection IDs |
| `getConnectedDeviceIds()` | List connected device IDs |

### Messaging

| Method | Description |
|--------|-------------|
| `send(id, payload, format, priority, requireAck, onDelivered)` | Full send |
| `send(id, payload, onDelivered)` | Simplified send (JSON, default priority) |
| `sendBinary(id, data, onDelivered)` | Send binary data |
| `sendBinary(id, data, priority, requireAck, onDelivered)` | Full binary send |

### Encryption

| Method | Description |
|--------|-------------|
| `setCryptoModule(module)` | Set encryption implementation |
| `addEncryptionKey(keyId, key)` | Register a pre-shared key |
| `removeEncryptionKey(keyId)` | Remove a key |
| `setDefaultEncryptionKey(keyId)` | Set default key for all connections |
| `setConnectionEncryptionKey(connId, keyId)` | Override key for specific connection |
| `enableEncryption(enabled)` | Enable/disable encryption |
| `isEncryptionEnabled()` | Check encryption status |

### Configuration

| Method | Description |
|--------|-------------|
| `setRetransmissionConfig(maxAttempts, interval)` | Configure retransmission |
| `setDefaultPriority(id, priority)` | Set connection default priority |
| `setOnTransportError(handler)` | Register transport error callback |
| `setOnMessageHandler(id, handler)` | Update message handler |
| `setOnDisconnected(id, handler)` | Update disconnect handler |

## Testing

```bash
cd build

# Run all tests
ctest --output-on-failure

# Run specific test suite
./test_crypto
./test_sdk
./test_transport_layer
./test_session_manager
./test_physical_layer

# With coverage
cmake .. -DENABLE_COVERAGE=ON
cmake --build . -j$(nproc)
ctest
gcovr --root .. --filter '../Sdk/' --filter '../Crypto_Module/' -r ..
```

## Project Structure

```
EminentFeedSystem/
├── Sdk/                    # Main user-facing API
├── Crypto_Module/          # Encryption (ChaCha20, Null, interface)
├── Session_Manager/        # Connection lifecycle, handshake
├── Transport_Layer/        # Reliable delivery, retransmission
├── Coding_Module/          # Message encoding/framing
├── Physical_Layer/         # UDP and in-memory transports
├── Validation_Module/      # Config validation
├── common/                 # Shared types, logging
├── app/                    # Demo applications
├── docs/                   # Architecture documentation
└── CMakeLists.txt          # Build system
```

## License

Proprietary — All rights reserved.
