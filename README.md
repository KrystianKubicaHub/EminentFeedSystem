# EminentFeedSystem SDK

> **Version 1.0.0** — Lightweight real-time peer-to-peer communication library for embedded systems and drones over WiFi.

## What is this?

A C++17 networking SDK for **encrypted bidirectional communication between ESP32 and Mac/PC over WiFi (UDP)**. Designed for drone-pilot, robot-controller, and IoT telemetry scenarios.

```
┌──────────────┐         WiFi (UDP)         ┌──────────────┐
│    ESP32     │ ◄──── encrypted ────────►   │   Mac / PC   │
│  gyroscope   │        ChaCha20             │   console    │
│  motor PWM   │                             │   commands   │
└──────────────┘                             └──────────────┘
```

## Features

- **WiFi UDP transport** — ESP32 ↔ Mac/PC, real-time, low-latency
- **Encrypted** — ChaCha20 stream cipher, 256-bit pre-shared keys, per-message nonce
- **Reliable delivery** — Configurable retransmission, ACK/NACK, message ordering
- **Heartbeat** — Automatic connection health monitoring, miss detection callbacks
- **Graceful disconnect** — Protocol-level disconnect with notifications
- **Multi-connection** — One device can maintain multiple simultaneous connections
- **Zero external dependencies** — Pure C++17, no OpenSSL or Boost

## Requirements

| Platform | Toolchain |
|----------|-----------|
| ESP32 | ESP-IDF v5.x (with C++17 support) |
| Mac/PC | CMake 3.10+, Clang/GCC with C++17 |

## Quick Start

### 1. ESP32 Side (Firmware)

Add EminentFeedSystem as an ESP-IDF component:

```bash
cd your_esp_project/components
ln -s /path/to/EminentFeedSystem/esp_idf_component eminent_sdk
```

In your firmware (`main.cpp`):

```cpp
#include "EminentSdk.hpp"
#include "PhysicalLayerEsp32Wifi.hpp"
#include "ChaCha20CryptoModule.hpp"

// After WiFi is connected:
EminentSdk sdk(5001, "192.168.1.100", 5000, LogLevel::INFO);

// Setup encryption
auto crypto = std::make_shared<ChaCha20CryptoModule>();
std::vector<uint8_t> key = { /* your 32-byte pre-shared key */ };
crypto->addKey(1, key);
sdk.setCryptoModule(crypto);
sdk.addEncryptionKey(1, key);
sdk.setDefaultEncryptionKey(1);
sdk.enableEncryption(true);

// Initialize
sdk.initialize(1, onSuccess, onFailure, onIncoming, onEstablished);

// Connect to Mac
sdk.connect(2, 5, onSuccess, onFailure, onTrouble,
            onDisconnected, onConnected, onMessage);

// Send telemetry
sdk.send(connectionId, "{\"gx\":1.5,\"gy\":-0.3}", nullptr);
```

Build with ESP-IDF:
```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

### 2. Mac/PC Side (Console App)

```cpp
#include "EminentSdk.hpp"
#include "PhysicalLayerUdp.hpp"
#include "ChaCha20CryptoModule.hpp"

// Same key as ESP32
EminentSdk sdk(5000, "192.168.1.200", 5001, LogLevel::WARN);

auto crypto = std::make_shared<ChaCha20CryptoModule>();
std::vector<uint8_t> key = { /* same 32-byte key as ESP32 */ };
crypto->addKey(1, key);
sdk.setCryptoModule(crypto);
sdk.addEncryptionKey(1, key);
sdk.setDefaultEncryptionKey(1);
sdk.enableEncryption(true);

sdk.initialize(2, onSuccess, onFailure, onIncoming, onEstablished);
sdk.connect(1, 5, ...);  // Connect to ESP32

// Receive telemetry
void onMessage(const Message& msg) {
    printf("Telemetry: %s\n", msg.payload.c_str());
}

// Send motor command
sdk.send(connectionId, "{\"speed\":128}", nullptr);
```

Build:
```bash
cd EminentFeedSystem
mkdir build && cd build
cmake -DBUILD_EXAMPLES=ON ..
make mac_console -j4
./mac_console 192.168.1.200
```

## Full Example: Gyroscope + Motor Control

See `examples/` for complete working code:

| Directory | Description |
|-----------|-------------|
| `examples/esp32_gyro_motor/` | ESP32 firmware: reads MPU6050 gyro, controls DC motor via PWM, sends telemetry at 10Hz |
| `examples/mac_console/` | Mac app: displays telemetry in terminal, accepts motor speed input (0-255) |

### Hardware Setup

```
ESP32 GPIO21 (SDA) ──── MPU6050 SDA
ESP32 GPIO22 (SCL) ──── MPU6050 SCL
ESP32 GPIO25 ─────────── L298N IN1 (motor driver)
ESP32 3.3V ──────────── MPU6050 VCC
ESP32 GND ───────────── MPU6050 GND, L298N GND
```

Both ESP32 and Mac must be on the **same WiFi network**.

## Architecture

```
┌─────────────────────────────────────────────┐
│               EminentSdk                     │  ← User API
├─────────────────────────────────────────────┤
│  Crypto Module (ChaCha20 / custom)          │  ← Encryption
├─────────────────────────────────────────────┤
│           Session Manager                    │  ← Connection lifecycle
├─────────────────────────────────────────────┤
│           Transport Layer                    │  ← Retransmission, ACK
├─────────────────────────────────────────────┤
│            Coding Module                     │  ← Framing, CRC
├─────────────────────────────────────────────┤
│     Physical Layer                           │  ← Network I/O
│  ┌─────────────────┬──────────────────┐     │
│  │ PhysicalLayerUdp│PhysicalLayerEsp32│     │
│  │   (Mac/Linux)   │   Wifi (ESP-IDF) │     │
│  └─────────────────┴──────────────────┘     │
└─────────────────────────────────────────────┘
```

## Encryption

- **Algorithm**: ChaCha20 (RFC 7539 core)
- **Key**: 256-bit pre-shared key (must be identical on both devices)
- **Nonce**: 96-bit random per message (no counter management needed)
- **Wire format**: `[keyId: 1B][nonce: 12B][ciphertext: NB]`
- **Scope**: Only user data (JSON, binary) is encrypted; protocol messages (handshake, heartbeat) are not

```cpp
// Both sides must have the same key:
std::vector<uint8_t> key(32);
// Fill with your secret (hardware RNG recommended)

auto crypto = std::make_shared<ChaCha20CryptoModule>();
crypto->addKey(1, key);

sdk.setCryptoModule(crypto);
sdk.addEncryptionKey(1, key);
sdk.setDefaultEncryptionKey(1);
sdk.enableEncryption(true);
```

### Custom Crypto Module

Implement `ICryptoModule` interface to swap in AES, hardware crypto, etc:

```cpp
class MyHardwareCrypto : public ICryptoModule {
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, uint8_t keyId) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& encrypted) override;
    void addKey(uint8_t keyId, const std::vector<uint8_t>& key) override;
    void removeKey(uint8_t keyId) override;
    bool hasKey(uint8_t keyId) const override;
};
```

## API Reference

### Connection Lifecycle

```cpp
// Initialize SDK
sdk.initialize(deviceId, onSuccess, onFailure, onIncomingDecision, onEstablished);

// Connect to peer
sdk.connect(targetDeviceId, priority, onSuccess, onFailure, onTrouble,
            onDisconnected, onConnected, onMessage,
            heartbeatInterval, onHeartbeatMissed, handshakeTimeout);

// Disconnect
sdk.disconnect(connectionId);

// Shutdown everything
sdk.shutdown();
```

### Sending Data

```cpp
// Send JSON (simple)
sdk.send(connectionId, "{\"temp\":25.3}", nullptr);

// Send with delivery confirmation
sdk.send(connectionId, payload, MessageFormat::JSON, priority,
         /*requireAck=*/true, []() { printf("Delivered!\n"); });

// Send binary data (video frames, sensor dumps, etc.)
std::vector<uint8_t> frame = { /* raw bytes */ };
sdk.sendBinary(connectionId, frame, nullptr);
```

### Configuration

```cpp
// Retransmission tuning
sdk.setRetransmissionConfig(/*maxAttempts=*/5, /*interval=*/200ms);

// Per-connection encryption key
sdk.setConnectionEncryptionKey(connectionId, keyId);

// Transport error callback
sdk.setOnTransportError([](const string& err) { /* handle */ });
```

## Integration into Your Project

### ESP-IDF (ESP32)

```bash
# Option A: Symlink into components/
cd your_project/components
ln -s /path/to/EminentFeedSystem/esp_idf_component eminent_sdk

# Option B: Set extra component dirs in your project CMakeLists.txt
set(EXTRA_COMPONENT_DIRS "/path/to/EminentFeedSystem/esp_idf_component")
```

### CMake (Mac/Linux/PC)

```cmake
add_subdirectory(path/to/EminentFeedSystem)
target_link_libraries(your_app
    eminent_sdk session_manager transport_layer
    physical_layer CodingModule common_utils crypto_module
)
```

## Building & Testing

```bash
git clone <repo-url>
cd EminentFeedSystem
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Run all tests (unit + integration)
ctest --output-on-failure --timeout 60

# Build with examples
cmake -DBUILD_EXAMPLES=ON ..
make mac_console
```

### Test Suites

| Suite | Type | Tests | What it covers |
|-------|------|-------|----------------|
| `test_crypto` | Unit | 16 | ChaCha20 encrypt/decrypt, key management |
| `test_transport_layer` | Unit | 11 | Framing, CRC, reliability |
| `test_session_manager` | Unit | 3 | Session state transitions |
| `test_physical_layer` | Unit | 6 | Network I/O abstraction |
| `test_integration_handshake` | Integration | 4 | 3-way handshake, timeout, rejection |
| `test_integration_encryption` | Integration | 5 | E2E encrypted messaging |
| `test_integration_disconnect` | Integration | 5 | Graceful disconnect, cleanup |
| `test_integration_heartbeat` | Integration | 4 | Keepalive, miss detection |
| `test_integration_retransmission` | Integration | 6 | Reliable delivery, ordering |
| **Total** | | **60** | |

## Project Structure

```
EminentFeedSystem/
├── Sdk/                        # Main user API (EminentSdk)
├── Crypto_Module/              # Encryption (ChaCha20, interface)
├── Session_Manager/            # Connection lifecycle
├── Transport_Layer/            # Reliable delivery
├── Coding_Module/              # Message framing
├── Physical_Layer/             # Network I/O
│   ├── PhysicalLayerUdp        # Mac/Linux (POSIX UDP sockets)
│   └── PhysicalLayerEsp32Wifi  # ESP32 (ESP-IDF lwIP sockets)
├── esp_idf_component/          # ESP-IDF component wrapper
├── examples/
│   ├── esp32_gyro_motor/       # Complete ESP32 firmware example
│   └── mac_console/            # Mac terminal app example
├── tests/
│   └── integration/            # SDK end-to-end tests (gtest)
└── CMakeLists.txt
```

## License

Proprietary — All rights reserved.
