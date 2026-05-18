#pragma once

// =============================================================================
// PROJECT SANDY — CENTRAL CONFIGURATION
// Edit this file to change any operational or hardware parameters.
// Sensor-specific constants (e.g. min warmup) are in each sensor's header.
// =============================================================================

// ── GPIO / HARDWARE ──────────────────────────────────────────────────────────

// I2C (shared by AS7341 and MS5837)
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

// DS18B20 1-Wire
#define PIN_DS18B20_DATA    4

// LED / power enable for AS7341 illumination source (set -1 to disable)
#define PIN_LED_ENABLE      -1

// ── TIMING & DUTY CYCLE ──────────────────────────────────────────────────────

// How long the system waits after powering sensors before taking readings (ms).
// Must be >= the largest _MIN_WARMUP_MS across all sensors.
#define SENSOR_WARMUP_MS        2000UL

// Interval between full measurement bursts (ms). 60000 = 1 minute.
#define SAMPLE_INTERVAL_MS      60000UL

// Number of individual readings averaged into one logged data point.
#define SAMPLES_PER_BURST       5

// Delay between individual readings within a burst (ms).
#define INTER_SAMPLE_DELAY_MS   200UL

// ── CALIBRATION ──────────────────────────────────────────────────────────────

// Set true to run calibration routines at startup (interactive via Serial).
#define RUN_CALIBRATION_ON_BOOT     false

// Set true to load saved calibration offsets from NVS/EEPROM on boot.
#define LOAD_CALIBRATION_FROM_NVS   false

// ── SERIAL ───────────────────────────────────────────────────────────────────

#define SERIAL_BAUD_RATE        115200
