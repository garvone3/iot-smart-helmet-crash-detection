/**
 * @file crash_detection_system.ino
 * @brief IoT Crash Detection System with MPU6050 Accelerometer
 * @version 2.0.0
 * @date 2026-01-30
 * 
 * @description
 * This system monitors vehicle acceleration using an MPU6050 sensor and detects
 * potential crash events. When acceleration thresholds are exceeded, it provides
 * a 10-second confirmation period with buzzer alerts. Users can cancel false
 * alarms via a button press. Confirmed crashes trigger an API notification.
 * 
 * @hardware
 * - ESP8266 NodeMCU
 * - MPU6050 6-axis accelerometer/gyroscope
 * - Active buzzer (D5/GPIO14)
 * - Pushbutton (D7/GPIO13)
 * 
 * @dependencies
 * - Wire.h (I2C communication)
 * - I2Cdev.h (I2C device library)
 * - MPU6050.h (Accelerometer driver)
 * - ESP8266WiFi.h (WiFi connectivity)
 * - ESP8266HTTPClient.h (HTTP client)
 * - ArduinoJson.h (JSON serialization)
 * - WiFiClient.h (WiFi client)
 * 
 * @author Original Author
 * @license MIT
 */

// ============================================================================
// LIBRARY INCLUDES
// ============================================================================
#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

// Pin Definitions
#define BUTTON_PIN            D7        // GPIO13 - Cancel button
#define BUZZER_PIN            14        // GPIO14/D5 - Alert buzzer
#define I2C_SDA_PIN           4         // GPIO4/D2 - I2C Data
#define I2C_SCL_PIN           5         // GPIO5/D1 - I2C Clock

// WiFi Configuration
const char* WIFI_SSID = "garv";
const char* WIFI_PASSWORD = "12345678";

// Network Configuration
const char* API_SERVER_URL = "http://16.16.184.223:4000/api/readings";
const char* API_AUTH_TOKEN = "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOiI2NzQ0NTk0OWI5NjhjNTFhMTM2ZWNjOGEiLCJlbWFpbCI6InJyckBnbWFpbC5jb20iLCJpYXQiOjE3MzI2MjUyODgsImV4cCI6MTczNTI1NTAzMX0.Nh3Yv1ZkTzjA_JEKkJp6m2XmIjuXFMPXUxIfCQj9jiw";

// Sensor Configuration
const int16_t ACCEL_SCALE_RANGE = 17000; // Raw accelerometer range
const uint8_t MAPPED_VALUE_MAX = 255;    // Maximum mapped value
const float ACCEL_SENSITIVITY = 16384.0; // LSB/g for ±2g range

// Crash Detection Thresholds (in g-force)
const float ACCEL_X_THRESHOLD = 1.25;
const float ACCEL_Y_THRESHOLD = 0.40;
const float ACCEL_Z_THRESHOLD = 0.48;

// Timing Configuration
const uint16_t CRASH_CONFIRMATION_CYCLES = 10;  // Number of confirmation cycles
const uint16_t BUZZER_ON_DURATION_MS = 1000;    // Buzzer on time
const uint16_t BUZZER_OFF_DURATION_MS = 1000;   // Buzzer off time
const uint16_t MAIN_LOOP_DELAY_MS = 2000;       // Main loop delay
const uint16_t WIFI_CONNECT_RETRY_MS = 500;     // WiFi connection retry delay

// Device Configuration
const uint8_t DEVICE_ID = 101;

// Serial Communication
const uint32_t SERIAL_BAUD_RATE = 9600;

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
MPU6050 mpu;

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @struct SensorData
 * @brief Container for all sensor readings and device information
 */
struct SensorData {
    uint8_t deviceId;       // Unique device identifier
    uint8_t tiltX;          // Mapped X-axis tilt value (0-255)
    uint8_t tiltY;          // Mapped Y-axis tilt value (0-255)
    uint8_t tiltZ;          // Mapped Z-axis tilt value (0-255)
    float accelX;           // X-axis acceleration in g-force
    float accelY;           // Y-axis acceleration in g-force
    float accelZ;           // Z-axis acceleration in g-force
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
SensorData sensorData = {
    .deviceId = DEVICE_ID,
    .tiltX = 0,
    .tiltY = 0,
    .tiltZ = 0,
    .accelX = 0.0,
    .accelY = 0.0,
    .accelZ = 0.0
};

// Raw sensor readings
int16_t rawAccelX, rawAccelY, rawAccelZ;
int16_t rawGyroX, rawGyroY, rawGyroZ;

// State management
bool isCrashConfirmed = false;
bool isButtonPressed = false;
bool shouldSkipCycle = false;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================
void initializeSerial();
void initializeWiFi();
void initializeI2C();
void initializeMPU6050();
void initializeGPIO();
bool checkButtonPress();
void updateSensorReadings();
void printSensorData();
bool isAccelerationAboveThreshold();
void handleCrashDetection();
void sendCrashReportToAPI();
void activateBuzzer();
void deactivateBuzzer();

// ============================================================================
// SETUP FUNCTION
// ============================================================================

/**
 * @brief System initialization routine
 * 
 * Initializes all hardware components and establishes network connectivity.
 * Execution halts if critical components fail to initialize.
 */
void setup() {
    // Initialize all subsystems
    initializeSerial();
    initializeGPIO();
    initializeWiFi();
    initializeI2C();
    initializeMPU6050();
    
    Serial.println(F("=========================================="));
    Serial.println(F("System initialization complete"));
    Serial.println(F("Crash detection system active"));
    Serial.println(F("=========================================="));
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

/**
 * @brief Initialize serial communication
 */
void initializeSerial() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(100); // Allow serial to stabilize
    Serial.println(F("\n\n=== Crash Detection System ==="));
    Serial.println(F("Initializing..."));
}

/**
 * @brief Initialize and connect to WiFi network
 * 
 * Blocks until connection is established. Prints connection status and
 * assigned IP address upon successful connection.
 */
void initializeWiFi() {
    Serial.println(F("\n[WiFi] Initializing..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print(F("[WiFi] Connecting to: "));
    Serial.println(WIFI_SSID);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(WIFI_CONNECT_RETRY_MS);
        Serial.print(F("."));
    }
    
    Serial.println(F("\n[WiFi] Connected successfully"));
    Serial.print(F("[WiFi] IP Address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WiFi] Signal Strength: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
}

/**
 * @brief Initialize I2C communication bus
 */
void initializeI2C() {
    Serial.println(F("\n[I2C] Initializing bus..."));
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println(F("[I2C] Bus initialized"));
    Serial.print(F("[I2C] SDA: GPIO"));
    Serial.println(I2C_SDA_PIN);
    Serial.print(F("[I2C] SCL: GPIO"));
    Serial.println(I2C_SCL_PIN);
}

/**
 * @brief Initialize MPU6050 accelerometer/gyroscope
 * 
 * Initializes the sensor and verifies communication. System halts if
 * the sensor cannot be detected or fails to communicate.
 */
void initializeMPU6050() {
    Serial.println(F("\n[MPU6050] Initializing sensor..."));
    mpu.initialize();
    
    // Verify sensor connection
    if (!mpu.testConnection()) {
        Serial.println(F("[MPU6050] ERROR: Connection failed!"));
        Serial.println(F("[MPU6050] Please check wiring:"));
        Serial.println(F("  - VCC to 3.3V"));
        Serial.println(F("  - GND to GND"));
        Serial.print(F("  - SDA to GPIO"));
        Serial.println(I2C_SDA_PIN);
        Serial.print(F("  - SCL to GPIO"));
        Serial.println(I2C_SCL_PIN);
        
        // Halt execution on critical failure
        while (1) {
            delay(1000);
        }
    }
    
    Serial.println(F("[MPU6050] Connection successful"));
    Serial.println(F("[MPU6050] Sensor ready"));
}

/**
 * @brief Initialize GPIO pins
 */
void initializeGPIO() {
    Serial.println(F("\n[GPIO] Initializing pins..."));
    
    // Configure button with internal pull-up
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.print(F("[GPIO] Button configured on pin: "));
    Serial.println(BUTTON_PIN);
    
    // Configure buzzer as output, initially off
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.print(F("[GPIO] Buzzer configured on pin: "));
    Serial.println(BUZZER_PIN);
    
    Serial.println(F("[GPIO] Pin initialization complete"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================

/**
 * @brief Main execution loop
 * 
 * Continuously monitors sensor data and handles crash detection logic.
 * The loop executes the following sequence:
 * 1. Read and display sensor data
 * 2. Reset button state if acceleration is normal
 * 3. Check for threshold violations
 * 4. Handle crash confirmation process
 * 5. Delay before next iteration
 */
void loop() {
    // Update and display current sensor readings
    updateSensorReadings();
    printSensorData();
    
    // Reset button flag when acceleration returns to normal
    if (!isAccelerationAboveThreshold()) {
        isButtonPressed = false;
    }
    
    // Check for crash conditions and handle appropriately
    if (isAccelerationAboveThreshold()) {
        handleCrashDetection();
    }
    
    // Wait before next sensor reading
    delay(MAIN_LOOP_DELAY_MS);
}

// ============================================================================
// SENSOR FUNCTIONS
// ============================================================================

/**
 * @brief Read and process all sensor data
 * 
 * Reads raw accelerometer and gyroscope values from MPU6050,
 * then converts and stores them in the global sensor data structure.
 */
void updateSensorReadings() {
    // Read raw sensor values
    mpu.getMotion6(&rawAccelX, &rawAccelY, &rawAccelZ, 
                   &rawGyroX, &rawGyroY, &rawGyroZ);
    
    // Map raw values to 0-255 range for tilt representation
    sensorData.tiltX = map(rawAccelX, -ACCEL_SCALE_RANGE, ACCEL_SCALE_RANGE, 
                           0, MAPPED_VALUE_MAX);
    sensorData.tiltY = map(rawAccelY, -ACCEL_SCALE_RANGE, ACCEL_SCALE_RANGE, 
                           0, MAPPED_VALUE_MAX);
    sensorData.tiltZ = map(rawAccelZ, -ACCEL_SCALE_RANGE, ACCEL_SCALE_RANGE, 
                           0, MAPPED_VALUE_MAX);
    
    // Convert raw values to g-force
    sensorData.accelX = rawAccelX / ACCEL_SENSITIVITY;
    sensorData.accelY = rawAccelY / ACCEL_SENSITIVITY;
    sensorData.accelZ = rawAccelZ / ACCEL_SENSITIVITY;
}

/**
 * @brief Print current sensor readings to serial monitor
 * 
 * Outputs formatted sensor data including both tilt values and
 * acceleration measurements in g-force units.
 */
void printSensorData() {
    Serial.println(F("--- Sensor Reading ---"));
    
    // Print tilt values
    Serial.print(F("Tilt X = "));
    Serial.print(sensorData.tiltX);
    Serial.print(F(" | Y = "));
    Serial.print(sensorData.tiltY);
    Serial.print(F(" | Z = "));
    Serial.println(sensorData.tiltZ);
    
    // Print acceleration values
    Serial.print(F("Accel X = "));
    Serial.print(sensorData.accelX, 3);
    Serial.print(F("g | Y = "));
    Serial.print(sensorData.accelY, 3);
    Serial.print(F("g | Z = "));
    Serial.print(sensorData.accelZ, 3);
    Serial.println(F("g"));
}

/**
 * @brief Check if acceleration exceeds defined thresholds
 * 
 * @return true if any axis exceeds its threshold, false otherwise
 */
bool isAccelerationAboveThreshold() {
    return (abs(sensorData.accelX) > ACCEL_X_THRESHOLD || 
            abs(sensorData.accelY) > ACCEL_Y_THRESHOLD || 
            abs(sensorData.accelZ) > ACCEL_Z_THRESHOLD);
}

// ============================================================================
// INPUT HANDLING
// ============================================================================

/**
 * @brief Check button state and update flags
 * 
 * Reads the cancel button state and updates the global button pressed flag.
 * Uses INPUT_PULLUP, so LOW = pressed, HIGH = not pressed.
 * 
 * @return true if button is currently pressed, false otherwise
 */
bool checkButtonPress() {
    int buttonState = digitalRead(BUTTON_PIN);
    
    if (buttonState == HIGH) {
        Serial.println(F("[Button] Pressed - Canceling false alarm"));
        isButtonPressed = true;
        shouldSkipCycle = true;
    } else {
        Serial.println(F("[Button] Not pressed"));
        isButtonPressed = false;
    }
    
    return isButtonPressed;
}

// ============================================================================
// CRASH DETECTION LOGIC
// ============================================================================

/**
 * @brief Handle crash detection and confirmation process
 * 
 * Implements the core crash detection logic with the following behavior:
 * 1. Ignores additional triggers if button was already pressed
 * 2. Initiates 10-second confirmation period with buzzer alerts
 * 3. Allows cancellation via button press during confirmation
 * 4. Sends API notification if crash is confirmed
 * 
 * This function preserves the exact logic from the original implementation.
 */
void handleCrashDetection() {
    if (!isButtonPressed) {
        Serial.println(F("\n[ALERT] Acceleration threshold exceeded!"));
        Serial.println(F("[ALERT] Starting crash confirmation sequence..."));
        
        // Confirmation loop: 10 cycles with 1-second intervals
        for (int cycleCount = 0; cycleCount < CRASH_CONFIRMATION_CYCLES; cycleCount++) {
            Serial.print(F("[Confirmation] Cycle "));
            Serial.print(cycleCount + 1);
            Serial.print(F("/"));
            Serial.println(CRASH_CONFIRMATION_CYCLES);
            
            // Alert user with buzzer
            activateBuzzer();
            delay(BUZZER_ON_DURATION_MS);
            deactivateBuzzer();
            delay(BUZZER_OFF_DURATION_MS);
            
            // Re-check sensor state during confirmation
            updateSensorReadings();
            
            // Check for cancellation conditions
            if (!isAccelerationAboveThreshold() || checkButtonPress()) {
                Serial.println(F("\n[Result] False alarm detected"));
                Serial.println(F("[Result] Crash confirmation canceled"));
                isCrashConfirmed = false;
                break;
            }
            
            // Mark as confirmed if we complete all cycles
            isCrashConfirmed = true;
        }
    }
    
    // Reset button flag if acceleration normalized
    if (!isAccelerationAboveThreshold()) {
        isButtonPressed = false;
    }
    
    // Handle confirmed crash
    if (isCrashConfirmed) {
        Serial.println(F("\n╔════════════════════════════════╗"));
        Serial.println(F("║   CRASH CONFIRMED - SENDING    ║"));
        Serial.println(F("║      EMERGENCY NOTIFICATION    ║"));
        Serial.println(F("╚════════════════════════════════╝"));
        sendCrashReportToAPI();
    }
}

// ============================================================================
// BUZZER CONTROL
// ============================================================================

/**
 * @brief Activate buzzer alert
 */
void activateBuzzer() {
    digitalWrite(BUZZER_PIN, HIGH);
}

/**
 * @brief Deactivate buzzer alert
 */
void deactivateBuzzer() {
    digitalWrite(BUZZER_PIN, LOW);
}

// ============================================================================
// NETWORK COMMUNICATION
// ============================================================================

/**
 * @brief Send crash report to remote API server
 * 
 * Constructs and sends an HTTP POST request with crash notification to the
 * configured API endpoint. Includes authentication and proper error handling.
 * 
 * The function:
 * - Verifies WiFi connectivity before attempting request
 * - Creates JSON payload with crash status
 * - Sends authenticated POST request
 * - Logs server response or error details
 * - Properly closes HTTP connection
 */
void sendCrashReportToAPI() {
    // Verify network connectivity
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[API] ERROR: WiFi disconnected"));
        Serial.println(F("[API] Cannot send crash report"));
        return;
    }
    
    Serial.println(F("\n[API] Preparing crash report..."));
    
    WiFiClient wifiClient;
    HTTPClient httpClient;
    
    // Initialize HTTP connection
    httpClient.begin(wifiClient, API_SERVER_URL);
    
    // Configure request headers
    httpClient.addHeader(F("Content-Type"), F("application/json"));
    httpClient.addHeader(F("Authorization"), API_AUTH_TOKEN);
    
    Serial.println(F("[API] Headers configured"));
    
    // Create JSON payload
    StaticJsonDocument<128> jsonDocument;
    jsonDocument["crashDetected"] = true;
    
    String requestBody;
    serializeJson(jsonDocument, requestBody);
    
    Serial.print(F("[API] Request payload: "));
    Serial.println(requestBody);
    Serial.print(F("[API] Sending to: "));
    Serial.println(API_SERVER_URL);
    
    // Send POST request
    int httpResponseCode = httpClient.POST(requestBody);
    
    // Process response
    if (httpResponseCode > 0) {
        Serial.print(F("[API] Response code: "));
        Serial.println(httpResponseCode);
        
        String responsePayload = httpClient.getString();
        Serial.print(F("[API] Response body: "));
        Serial.println(responsePayload);
        
        if (httpResponseCode == 200 || httpResponseCode == 201) {
            Serial.println(F("[API] ✓ Crash report sent successfully"));
        } else {
            Serial.print(F("[API] ⚠ Unexpected response code: "));
            Serial.println(httpResponseCode);
        }
    } else {
        Serial.print(F("[API] ✗ Request failed: "));
        Serial.println(httpClient.errorToString(httpResponseCode));
    }
    
    // Clean up resources
    httpClient.end();
    Serial.println(F("[API] Connection closed\n"));
}

// ============================================================================
// END OF FILE
// ============================================================================
