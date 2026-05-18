#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// =====================
// CONFIG DEFINES
// =====================

#define DEPLOYMENT_ID "BUOY-001"

#define WAKE_INTERVAL_S 3600

#define LOG_DIR  "/data"
#define LOG_FILE "/data/buoy.csv"

#define AP_SSID     "BUOY-001-RECOVERY"
#define AP_PASSWORD "password"

#define RETRIEVAL_TIMEOUT_MS 600000UL

#define OVERSAMPLE_COUNT 16
#define SAMPLE_SPACING_MS 50
#define TRIM_FRACTION 0.20f

#define RETRIEVAL_TRIGGER_PIN 0

#include "config.h"
#include "DS18B20_sensor.h"
#include "MS5837_sensor.h"
#include "AS7341_sensor.h"

// =====================
// LOGGER FILES
// =====================

#include "sensors.h"
#include "dsp.h"
#include "storage.h"
#include "retrieval.h"

// =====================
// GPIO
// =====================

#define MOTOR_PIN 5
#define GPIO23 23
#define PIN_TIA 34

// =====================
// Retrieval / Logging Config
// =====================

#define DEPLOYMENT_EPOCH 1747267200

// =====================
// Sensors
// =====================

DS18B20Sensor ds18b20;
MS5837Sensor ms5837;
AS7341Sensor as7341;

// =====================
// State Machine
// =====================

enum State {
    IDLE,
    MOVE_DEPTH,
    MEASURE,
    WAIT,
    EMERGENCY
};

State currentState = MOVE_DEPTH;

// =====================
// Depth Profiling
// =====================

float depthLevels[] = {0.5, 1.5, 2.5, 3.0};

int currentTargetIndex = 0;

float targetDepth = depthLevels[0];

float simulatedDepth = 0.0;

// =====================
// Timing
// =====================

unsigned long previousMillis = 0;

const unsigned long waitInterval = 6000;

// =====================
// Battery Model
// =====================

float batteryCapacity_mAh = 6000.0;
float batteryRemaining_mAh = 6000.0;

float batteryVoltage = 8.4;

unsigned long lastBatteryUpdate = 0;

// =====================
// Battery Update
// =====================

void updateBattery(float current_mA) {

    unsigned long now = millis();

    float deltaHours =
        (now - lastBatteryUpdate) / 3600000.0;

    lastBatteryUpdate = now;

    batteryRemaining_mAh -=
        current_mA * deltaHours;

    if (batteryRemaining_mAh < 0)
        batteryRemaining_mAh = 0;

    float soc =
        batteryRemaining_mAh /
        batteryCapacity_mAh;

    // Approximate 2S Li-ion curve
    batteryVoltage =
        6.0 + (soc * 2.4);
}

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR time_t lastEpoch = 0;

//=======================
// Counters
//=======================

int count1 = 0;
int count2 = 0;
int count3 = 0;
int count4 = 0;

//==========================

bool retrievalRequested() {

    pinMode(RETRIEVAL_TRIGGER_PIN, INPUT_PULLUP);

    if (digitalRead(RETRIEVAL_TRIGGER_PIN) != HIGH)
        return false;

    delay(1500);

    return digitalRead(RETRIEVAL_TRIGGER_PIN) == HIGH;
}

// =====================
// Setup
// =====================

void setup() {

    bootCount++;

    Serial.begin(115200);

    delay(2000);

    if (!LittleFS.begin(true)) {

        Serial.println("LittleFS mount failed");
    }

    if (retrievalRequested()) {

        enterRetrievalMode();
    }

    analogSetAttenuation(ADC_11db);

    pinMode(MOTOR_PIN, OUTPUT);

    pinMode(GPIO23, OUTPUT);

    // Initialize sensors
    ds18b20.begin();

    ms5837.begin();

    as7341.begin();

    lastBatteryUpdate = millis();

    Serial.println("=================================");
    Serial.println("PROJECT SANDY INITIALISED");
    Serial.println("=================================");
}

// =====================
// Main Loop
// =====================

void loop() {

    // Emergency trigger
    if (batteryVoltage < 6.6) {
        currentState = EMERGENCY;
    }

    switch (currentState) {

        // =====================================
        // MOVE TO DEPTH
        // =====================================

        case MOVE_DEPTH: {

            updateBattery(330);

            if (count2 == 0){
            Serial.print("Moving to depth: ");
            Serial.println(targetDepth);
            count2 = 1;
            }

            digitalWrite(MOTOR_PIN, HIGH);

            // Simulated movement
            if (simulatedDepth < targetDepth) {
                simulatedDepth += 0.02;
            }
            else if (simulatedDepth > targetDepth) {
                simulatedDepth -= 0.01;
            }

            if (count1 % 10 == 0) {
            Serial.print("Current Depth: ");
            Serial.println(simulatedDepth); 
            }
            
            count1 = count1 + 1;

            // Reached target
            if (abs(simulatedDepth - targetDepth) < 0.02) {

                digitalWrite(MOTOR_PIN, LOW);

                Serial.println("Target depth reached");

                count2 = 0;

                currentState = MEASURE;
            }

            delay(100);

            break;
        }
        // =====================================
        // MEASURE
        // =====================================

        case MEASURE: {

            updateBattery(606);

            Serial.println("Taking measurements...");
            Serial.println("--------------------------------");

            digitalWrite(GPIO23, HIGH);

            Reading r;

            // Timestamp
            if (lastEpoch == 0)
                lastEpoch = DEPLOYMENT_EPOCH;
            else
                lastEpoch += WAKE_INTERVAL_S;

            r.epoch = lastEpoch;

            // ================= DS18B20 =================

            DS18B20Data tempData = ds18b20.read();

            if (tempData.valid) {

                ds18b20.printData(tempData);
                //r.temp_c = tempData.temperatureC;
                r.temp_c = tempData.temperature_C;
            }

            // ================= MS5837 =================

            MS5837Data pressureData = ms5837.read();

            if (pressureData.valid) {

                ms5837.printData(pressureData);

                r.pressure_kpa = pressureData.pressure_mbar / 10.0;
                r.depth_m = simulatedDepth;
                //r.depth_m = pressureData.depth_m;
            }

            // ================= AS7341 =================

            AS7341Data spectralData = as7341.read();

            if (spectralData.valid) {

                as7341.printData(spectralData);

                r.f1 = spectralData.F1_415nm;
                r.f2 = spectralData.F2_445nm;
                r.f3 = spectralData.F3_480nm;
                r.f4 = spectralData.F4_515nm;
                r.f5 = spectralData.F5_555nm;
                r.f6 = spectralData.F6_590nm;
                r.f7 = spectralData.F7_630nm;
                r.f8 = spectralData.F8_680nm;
            }

            // ================= BPW34 =================

            int tiaRaw = analogRead(PIN_TIA);

            float tiaVoltage = tiaRaw * (3.3f / 4095.0f);

            r.tia_voltage = tiaVoltage;

            Serial.print("[BPW34] raw=");
            Serial.print(tiaRaw);

            Serial.print(" voltage=");
            Serial.print(tiaVoltage, 4);

            Serial.println(" V");

            // ================= Battery =================

            r.battery_v = batteryVoltage;

            Serial.print("Battery Voltage: ");
            Serial.println(batteryVoltage);

            // ================= Log Data =================

            if (logReading(r)) {

                Serial.println("Data logged to CSV");
            }
            else {

                Serial.println("CSV logging failed");
            }

            Serial.println("--------------------------------");
            Serial.println("");

            digitalWrite(GPIO23, LOW);

            previousMillis = millis();

            currentState = WAIT;

            break;
        }

        // =====================================
        // WAIT
        // =====================================

        case WAIT:{

            updateBattery(6);

            if (millis() - previousMillis >= waitInterval) {

                currentTargetIndex++;

                if (currentTargetIndex >= 4) {
                    currentTargetIndex = 0;
                }

                targetDepth =
                    depthLevels[currentTargetIndex];

                Serial.print("Next Target Depth: ");

                Serial.println(targetDepth);

                currentState = MOVE_DEPTH;
            }

            break;
        }

        // =====================================
        // EMERGENCY
        // =====================================

        case EMERGENCY:{

            updateBattery(300);

            if (count3 == 0){
            Serial.println("!!! EMERGENCY MODE !!!");
            count3 = 1;
            }

            digitalWrite(GPIO23, LOW);

            digitalWrite(MOTOR_PIN, HIGH);

            // Surface
            if (simulatedDepth > 0.0) {
                simulatedDepth -= 0.02;
            }

            if (count4 % 10 == 0){
            Serial.print("Surfacing... Depth: ");
            Serial.println(simulatedDepth);
            }
            count4 = count4 + 1;

            if (simulatedDepth <= 0.05) {

                digitalWrite(MOTOR_PIN, LOW);
                delay(10000);
                currentState = IDLE;
            }

            delay(100);

            break;
        }

        // =====================================
        // IDLE
        // =====================================

        case IDLE:{

            updateBattery(2);

            delay(10000);

            break;
        }
    }
}