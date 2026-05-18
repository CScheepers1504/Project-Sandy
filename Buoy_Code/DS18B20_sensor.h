#pragma once

// =============================================================================
// DS18B20 — 1-Wire Digital Temperature Sensor
// Libraries: OneWire + DallasTemperature  (install both via Library Manager)
//
// Wiring (1-Wire):
//   DATA → PIN_DS18B20_DATA  (config.h)
//   VCC  → 3.3 V  (parasitic power NOT recommended for ESP32)
//   GND  → GND
//   Pull-up: 4.7 kΩ between DATA and VCC  (required)
//
// Multiple sensors can share the same 1-Wire bus.
// This driver addresses sensor 0 by index; use getDeviceCount() to confirm
// how many probes are found, and setTargetSensor() to select a different one.
//
// Calibration:
//   Ice-bath (0 °C) or boiling-point method — call calibrateWithReference()
//   with the known temperature. The offset is stored and added to every read.
// =============================================================================

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

// ── SENSOR-SPECIFIC SETTINGS (edit here) ─────────────────────────────────────

// Resolution: 9 = 0.5 °C (93 ms),  10 = 0.25 °C (188 ms),
//             11 = 0.125 °C (375 ms),  12 = 0.0625 °C (750 ms)
#define DS18B20_RESOLUTION      12

// Samples averaged for a calibration reference pass
#define DS18B20_CAL_SAMPLES     10

// Minimum warm-up time after power-on (ms)
#define DS18B20_MIN_WARMUP_MS   200UL

// ── CONSTANTS (do not edit) ───────────────────────────────────────────────────

// Conversion time in ms for each resolution (DS18B20 datasheet Table 2).
// 750 / 2^(12 - resolution)
#define DS18B20_CONVERSION_MS   (750u >> (12 - DS18B20_RESOLUTION))

// ── DATA STRUCTURE ────────────────────────────────────────────────────────────

struct DS18B20Data {
    float temperature_C;     // Calibrated temperature
    float rawTemperature_C;  // Uncorrected temperature (diagnostic)
    bool  valid;
};

// ── CLASS ─────────────────────────────────────────────────────────────────────

class DS18B20Sensor {
public:

    DS18B20Sensor()
        : _oneWire(PIN_DS18B20_DATA), _sensors(&_oneWire) {}

    // ── Lifecycle ────────────────────────────────────────────────────────────

    bool begin() {
        _sensors.begin();
        _deviceCount = _sensors.getDeviceCount();

        if (_deviceCount == 0) {
            Serial.println("[DS18B20] No devices found — check wiring and pull-up resistor");
            return false;
        }

        _sensors.setResolution(DS18B20_RESOLUTION);
        _sensors.setWaitForConversion(false);  // Non-blocking; we delay manually.

        _initialized = true;
        Serial.printf("[DS18B20] Found %u device(s) on bus, resolution=%d-bit (%u ms)\n",
                      _deviceCount, DS18B20_RESOLUTION, DS18B20_CONVERSION_MS);
        return true;
    }

    // Select which sensor index on the bus to read (default 0).
    void setTargetSensor(uint8_t index) {
        _targetIndex = (index < _deviceCount) ? index : 0;
    }

    uint8_t getDeviceCount() const { return _deviceCount; }

    // ── Calibration ─────────────────────────────────────────────────────────

    // Place the sensor in a reference medium (e.g. ice bath = 0 °C),
    // wait for thermal equilibrium, then call with the known temperature.
    // Computes and stores a fixed offset applied to every subsequent read().
    bool calibrateWithReference(float referenceTemp_C) {
        if (!_initialized) return false;

        Serial.printf("[DS18B20] Calibrating against %.4f °C reference (%d samples)...\n",
                      referenceTemp_C, DS18B20_CAL_SAMPLES);

        double sum = 0.0;
        for (int i = 0; i < DS18B20_CAL_SAMPLES; i++) {
            float raw = _readRaw();
            if (raw == DEVICE_DISCONNECTED_C) {
                Serial.println("[DS18B20] Disconnected during calibration");
                return false;
            }
            sum += raw;
            delay(DS18B20_CONVERSION_MS + 10);
        }

        float rawMean    = static_cast<float>(sum / DS18B20_CAL_SAMPLES);
        _offsetC         = referenceTemp_C - rawMean;
        _userCalibrated  = true;

        Serial.printf("[DS18B20] Offset set to %.4f °C (raw mean=%.4f)\n",
                      _offsetC, rawMean);
        return true;
    }

    // Override the offset directly (e.g. loaded from NVS).
    void setOffset(float offsetC) {
        _offsetC        = offsetC;
        _userCalibrated = true;
    }

    float getOffset() const { return _offsetC; }

    // ── Reading ──────────────────────────────────────────────────────────────

    DS18B20Data read() {
        DS18B20Data d = {};

        if (!_initialized) {
            Serial.println("[DS18B20] Not initialized");
            return d;
        }

        float raw = _readRaw();

        if (raw == DEVICE_DISCONNECTED_C || raw == 85.0f) {
            // 85 °C is the DS18B20 power-on default — indicates a failed conversion.
            Serial.println("[DS18B20] Sensor disconnected or conversion error");
            return d;
        }

        d.rawTemperature_C = raw;
        d.temperature_C    = raw + _offsetC;
        d.valid            = true;
        return d;
    }

    // Returns the average of n readings. Skips disconnected reads.
    DS18B20Data readAveraged(uint8_t n, uint16_t delayBetweenMs = 0) {
        double   sum  = 0.0;
        double   sumR = 0.0;
        uint8_t  good = 0;

        for (uint8_t i = 0; i < n; i++) {
            DS18B20Data r = read();
            if (!r.valid) { delay(DS18B20_CONVERSION_MS + 10); continue; }
            sum  += r.temperature_C;
            sumR += r.rawTemperature_C;
            good++;
            if (i < n - 1 && delayBetweenMs > 0) delay(delayBetweenMs);
        }

        if (good == 0) return {};

        DS18B20Data avg;
        avg.temperature_C    = static_cast<float>(sum  / good);
        avg.rawTemperature_C = static_cast<float>(sumR / good);
        avg.valid            = true;
        return avg;
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    bool isInitialized()    const { return _initialized; }
    bool isUserCalibrated() const { return _userCalibrated; }

    void printData(const DS18B20Data& d) {
        if (!d.valid) { Serial.println("[DS18B20] Invalid / disconnected"); return; }
        Serial.printf("[DS18B20] Temp=%.4f °C  (raw=%.4f, offset=%.4f)\n",
                      d.temperature_C, d.rawTemperature_C, _offsetC);
    }

private:
    OneWire          _oneWire;
    DallasTemperature _sensors;

    uint8_t  _deviceCount   = 0;
    uint8_t  _targetIndex   = 0;
    float    _offsetC       = 0.0f;
    bool     _initialized   = false;
    bool     _userCalibrated = false;

    float _readRaw() {
        _sensors.requestTemperatures();
        delay(DS18B20_CONVERSION_MS + 5);  // +5 ms margin
        return _sensors.getTempCByIndex(_targetIndex);
    }
};
