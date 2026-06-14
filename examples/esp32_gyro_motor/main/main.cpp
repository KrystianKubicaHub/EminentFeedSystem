/**
 * ============================================================
 * ESP32 Example: Gyroscope Telemetry + Motor Control
 * ============================================================
 *
 * Hardware:
 *   - ESP32 (any variant: ESP32, ESP32-S3, etc.)
 *   - MPU6050 IMU (I2C: SDA=GPIO21, SCL=GPIO22)
 *   - DC Motor via L298N driver (PWM: GPIO25)
 *   - WiFi connection to the same network as Mac
 *
 * This firmware:
 *   1. Connects to WiFi
 *   2. Establishes encrypted connection with Mac app via EminentSdk
 *   3. Sends gyroscope readings as JSON every 100ms
 *   4. Receives motor speed commands from Mac
 *
 * Build with ESP-IDF:
 *   idf.py set-target esp32
 *   idf.py build
 *   idf.py flash monitor
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

// ESP-IDF includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "driver/ledc.h"

// EminentFeedSystem
#include "EminentSdk.hpp"
#include "PhysicalLayerEsp32Wifi.hpp"
#include "ChaCha20CryptoModule.hpp"

static const char* TAG = "GyroMotor";

// ============================================================
// Configuration — CHANGE THESE FOR YOUR SETUP
// ============================================================
#define WIFI_SSID       "YourNetworkSSID"
#define WIFI_PASS       "YourNetworkPassword"
#define MAC_IP_ADDRESS  "192.168.1.100"   // IP of the Mac running mac_console
#define MAC_PORT        5000              // Port the Mac listens on
#define ESP32_PORT      5001              // Port the ESP32 listens on
#define DEVICE_ID       1                 // This device's ID
#define MAC_DEVICE_ID   2                 // Mac device's ID

// I2C config for MPU6050
#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_SDA     GPIO_NUM_21
#define I2C_MASTER_SCL     GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ 400000
#define MPU6050_ADDR       0x68

// Motor PWM config
#define MOTOR_PWM_GPIO     GPIO_NUM_25
#define MOTOR_PWM_CHANNEL  LEDC_CHANNEL_0
#define MOTOR_PWM_FREQ     5000
#define MOTOR_PWM_RES      LEDC_TIMER_8_BIT

// Pre-shared encryption key (256 bits) — MUST match mac_console
static const uint8_t SHARED_KEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

// ============================================================
// Global state
// ============================================================
static std::atomic<int> g_motorSpeed{0};       // 0-255
static std::atomic<bool> g_connected{false};
static ConnectionId g_connectionId = -1;

// ============================================================
// WiFi initialization (STA mode)
// ============================================================
static void wifi_init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
    vTaskDelay(pdMS_TO_TICKS(5000)); // Simple wait for connection
    ESP_LOGI(TAG, "WiFi connected (assuming success)");
}

// ============================================================
// I2C + MPU6050 initialization
// ============================================================
static void i2c_init() {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA;
    conf.scl_io_num = I2C_MASTER_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    // Wake up MPU6050 (write 0 to power management register 0x6B)
    uint8_t data[2] = {0x6B, 0x00};
    i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, data, 2, pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "MPU6050 initialized");
}

struct GyroData {
    float gx, gy, gz;  // degrees/sec
    float ax, ay, az;  // g
};

static GyroData read_mpu6050() {
    uint8_t reg = 0x3B; // ACCEL_XOUT_H
    uint8_t buf[14];

    i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR,
                                  &reg, 1, buf, 14, pdMS_TO_TICKS(100));

    GyroData data;
    int16_t ax = (buf[0] << 8) | buf[1];
    int16_t ay = (buf[2] << 8) | buf[3];
    int16_t az = (buf[4] << 8) | buf[5];
    int16_t gx = (buf[8] << 8) | buf[9];
    int16_t gy = (buf[10] << 8) | buf[11];
    int16_t gz = (buf[12] << 8) | buf[13];

    data.ax = ax / 16384.0f;
    data.ay = ay / 16384.0f;
    data.az = az / 16384.0f;
    data.gx = gx / 131.0f;
    data.gy = gy / 131.0f;
    data.gz = gz / 131.0f;

    return data;
}

// ============================================================
// Motor PWM initialization
// ============================================================
static void motor_init() {
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.timer_num = LEDC_TIMER_0;
    timer_conf.duty_resolution = MOTOR_PWM_RES;
    timer_conf.freq_hz = MOTOR_PWM_FREQ;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {};
    ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_conf.channel = MOTOR_PWM_CHANNEL;
    ch_conf.timer_sel = LEDC_TIMER_0;
    ch_conf.gpio_num = MOTOR_PWM_GPIO;
    ch_conf.duty = 0;
    ch_conf.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));

    ESP_LOGI(TAG, "Motor PWM initialized on GPIO%d", MOTOR_PWM_GPIO);
}

static void motor_set_speed(int speed) {
    if (speed < 0) speed = 0;
    if (speed > 255) speed = 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_PWM_CHANNEL);
}

// ============================================================
// Message handler — receives motor commands from Mac
// Expected JSON format: {"speed": 128}
// ============================================================
static void on_message_received(const Message& msg) {
    // Simple JSON parsing for {"speed": NNN}
    std::string payload = msg.payload;
    size_t pos = payload.find("\"speed\"");
    if (pos != std::string::npos) {
        size_t colon = payload.find(":", pos);
        if (colon != std::string::npos) {
            int speed = atoi(payload.c_str() + colon + 1);
            g_motorSpeed = speed;
            motor_set_speed(speed);
            ESP_LOGI(TAG, "Motor speed set to: %d", speed);
        }
    }
}

// ============================================================
// Main application
// ============================================================
extern "C" void app_main() {
    ESP_LOGI(TAG, "=== EminentFeedSystem ESP32 Example: Gyro + Motor ===");
    ESP_LOGI(TAG, "SDK Version: %s", EminentSdk::version());

    // 1. Initialize hardware
    wifi_init();
    i2c_init();
    motor_init();

    // 2. Create SDK with ESP32 WiFi physical layer
    auto sdk = std::make_unique<EminentSdk>(
        ESP32_PORT, MAC_IP_ADDRESS, MAC_PORT, LogLevel::INFO
    );

    // 3. Setup encryption
    auto crypto = std::make_shared<ChaCha20CryptoModule>();
    std::vector<uint8_t> key(SHARED_KEY, SHARED_KEY + 32);
    crypto->addKey(1, key);

    sdk->setCryptoModule(crypto);
    sdk->addEncryptionKey(1, key);
    sdk->setDefaultEncryptionKey(1);
    sdk->enableEncryption(true);

    // 4. Initialize SDK
    sdk->initialize(
        DEVICE_ID,
        []() { ESP_LOGI(TAG, "SDK initialized successfully"); },
        [](const std::string& err) { ESP_LOGE(TAG, "SDK init failed: %s", err.c_str()); },
        [](DeviceId id, const std::string&) {
            ESP_LOGI(TAG, "Incoming connection from device %d — accepting", id);
            return true;
        },
        [](ConnectionId cid, DeviceId did) {
            ESP_LOGI(TAG, "Connection %d established with device %d", cid, did);
            g_connected = true;
            g_connectionId = cid;
        }
    );

    // 5. Connect to Mac
    sdk->connect(
        MAC_DEVICE_ID, 5,
        [](ConnectionId cid) {
            ESP_LOGI(TAG, "Connected! ConnectionId=%d", cid);
            g_connectionId = cid;
            g_connected = true;
        },
        [](const std::string& err) {
            ESP_LOGE(TAG, "Connection failed: %s", err.c_str());
        },
        [](const std::string& msg) {
            ESP_LOGW(TAG, "Trouble: %s", msg.c_str());
        },
        []() {
            ESP_LOGW(TAG, "Disconnected from Mac");
            g_connected = false;
        },
        [](ConnectionId cid) {
            ESP_LOGI(TAG, "Connection active: %d", cid);
            g_connectionId = cid;
            g_connected = true;
        },
        on_message_received,
        std::chrono::milliseconds{1000},  // Heartbeat every 1s
        [](ConnectionId) {
            ESP_LOGW(TAG, "Heartbeat missed!");
        },
        std::chrono::milliseconds{10000}
    );

    // 6. Main loop: read gyroscope, send telemetry
    ESP_LOGI(TAG, "Entering main loop — sending gyro data every 100ms");

    while (true) {
        if (g_connected && g_connectionId > 0) {
            GyroData gyro = read_mpu6050();

            // Format as JSON telemetry
            char json[256];
            snprintf(json, sizeof(json),
                "{\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
                "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
                "\"motor\":%d}",
                gyro.gx, gyro.gy, gyro.gz,
                gyro.ax, gyro.ay, gyro.az,
                g_motorSpeed.load());

            try {
                sdk->send(g_connectionId, std::string(json), nullptr);
            } catch (const std::exception& ex) {
                ESP_LOGW(TAG, "Send failed: %s", ex.what());
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz telemetry rate
    }
}
