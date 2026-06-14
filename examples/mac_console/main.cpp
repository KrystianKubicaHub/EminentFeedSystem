/**
 * ============================================================
 * Mac Console Example: Telemetry Display + Motor Control
 * ============================================================
 *
 * This application:
 *   1. Connects to ESP32 via WiFi (UDP)
 *   2. Displays gyroscope telemetry in the terminal
 *   3. Allows typing motor speed commands (0-255)
 *
 * Build:
 *   cd EminentFeedSystem
 *   mkdir build_example && cd build_example
 *   cmake -DBUILD_EXAMPLES=ON ..
 *   make mac_console
 *
 * Run:
 *   ./mac_console
 *
 * Usage:
 *   - Telemetry from ESP32 is printed automatically
 *   - Type a number (0-255) and press Enter to set motor speed
 *   - Type 'q' to quit
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
#include <sstream>

#include "EminentSdk.hpp"
#include "PhysicalLayerUdp.hpp"
#include "ChaCha20CryptoModule.hpp"

using namespace std;
using namespace chrono;

// ============================================================
// Configuration — MUST MATCH ESP32 settings
// ============================================================
static const char* ESP32_IP   = "192.168.1.200";  // IP of the ESP32
static const int   ESP32_PORT = 5001;             // Port ESP32 listens on
static const int   MAC_PORT   = 5000;             // Port this Mac listens on
static const int   DEVICE_ID  = 2;                // This device's ID
static const int   ESP_DEVICE_ID = 1;             // ESP32 device's ID

// Pre-shared encryption key (256 bits) — MUST match ESP32
static const uint8_t SHARED_KEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

// ============================================================
// Global state
// ============================================================
static atomic<bool> g_connected{false};
static atomic<bool> g_running{true};
static ConnectionId g_connectionId = -1;

// ============================================================
// Telemetry display
// ============================================================
static void on_message_received(const Message& msg) {
    // Clear line and print telemetry
    printf("\r\033[K");  // Clear current line
    printf("📡 %s", msg.payload.c_str());
    printf("\n> ");
    fflush(stdout);
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    printf("============================================================\n");
    printf("  EminentFeedSystem — Mac Console (Telemetry + Motor Control)\n");
    printf("  SDK Version: %s\n", EminentSdk::version());
    printf("============================================================\n\n");

    // Allow overriding ESP32 IP from command line
    string esp32Ip = ESP32_IP;
    if (argc > 1) {
        esp32Ip = argv[1];
        printf("Using ESP32 IP: %s\n", esp32Ip.c_str());
    }

    // 1. Create SDK with UDP physical layer
    printf("[*] Creating SDK on port %d, remote=%s:%d\n", MAC_PORT, esp32Ip.c_str(), ESP32_PORT);
    EminentSdk sdk(MAC_PORT, esp32Ip, ESP32_PORT, LogLevel::WARN);

    // 2. Setup encryption
    auto crypto = make_shared<ChaCha20CryptoModule>();
    vector<uint8_t> key(SHARED_KEY, SHARED_KEY + 32);
    crypto->addKey(1, key);

    sdk.setCryptoModule(crypto);
    sdk.addEncryptionKey(1, key);
    sdk.setDefaultEncryptionKey(1);
    sdk.enableEncryption(true);
    printf("[*] Encryption enabled (ChaCha20, keyId=1)\n");

    // 3. Initialize SDK
    sdk.initialize(
        DEVICE_ID,
        []() { printf("[✓] SDK initialized\n"); },
        [](const string& err) { printf("[✗] SDK init failed: %s\n", err.c_str()); },
        [](DeviceId id, const string&) {
            printf("[*] Incoming connection from device %d — accepting\n", id);
            return true;
        },
        [](ConnectionId cid, DeviceId did) {
            printf("[✓] Connection %d established with device %d\n", cid, did);
            g_connected = true;
            g_connectionId = cid;
        }
    );

    // 4. Connect to ESP32
    printf("[*] Connecting to ESP32 (device %d)...\n", ESP_DEVICE_ID);
    sdk.connect(
        ESP_DEVICE_ID, 5,
        [](ConnectionId cid) {
            printf("[✓] Connected! ConnectionId=%d\n", cid);
            g_connectionId = cid;
            g_connected = true;
        },
        [](const string& err) {
            printf("[✗] Connection failed: %s\n", err.c_str());
        },
        [](const string& msg) {
            printf("[!] Warning: %s\n", msg.c_str());
        },
        []() {
            printf("[!] Disconnected from ESP32\n");
            g_connected = false;
        },
        [](ConnectionId cid) {
            printf("[✓] Connection active: %d\n", cid);
            g_connectionId = cid;
            g_connected = true;
        },
        on_message_received,
        milliseconds{1000},
        [](ConnectionId) {
            printf("\n[!] Heartbeat missed — ESP32 may be offline\n> ");
            fflush(stdout);
        },
        milliseconds{10000}
    );

    // 5. Wait for connection
    printf("[*] Waiting for connection...\n");
    auto start = steady_clock::now();
    while (!g_connected && (steady_clock::now() - start < 15s)) {
        this_thread::sleep_for(100ms);
    }

    if (!g_connected) {
        printf("[✗] Could not connect to ESP32 within 15 seconds\n");
        printf("    Make sure:\n");
        printf("    - ESP32 is powered on and running\n");
        printf("    - Both devices are on the same WiFi network\n");
        printf("    - ESP32 IP is correct (%s)\n", esp32Ip.c_str());
        sdk.shutdown();
        return 1;
    }

    // 6. Interactive loop
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Connected! Telemetry will appear above.\n");
    printf("  Type motor speed (0-255) and press Enter to control motor.\n");
    printf("  Type 'q' to quit.\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    while (g_running) {
        printf("> ");
        fflush(stdout);

        string input;
        if (!getline(cin, input)) break;

        if (input == "q" || input == "quit" || input == "exit") {
            break;
        }

        if (input.empty()) continue;

        // Parse motor speed
        try {
            int speed = stoi(input);
            if (speed < 0 || speed > 255) {
                printf("  ⚠ Speed must be 0-255\n");
                continue;
            }

            if (!g_connected || g_connectionId <= 0) {
                printf("  ⚠ Not connected\n");
                continue;
            }

            // Send motor command as JSON
            string cmd = "{\"speed\":" + to_string(speed) + "}";
            sdk.send(g_connectionId, cmd, nullptr);
            printf("  → Motor speed set to %d\n", speed);

        } catch (const exception&) {
            printf("  ⚠ Invalid input. Enter a number (0-255) or 'q' to quit.\n");
        }
    }

    // 7. Cleanup
    printf("\n[*] Shutting down...\n");
    sdk.disconnect(g_connectionId);
    this_thread::sleep_for(200ms);
    sdk.shutdown();
    printf("[✓] Goodbye!\n");

    return 0;
}
