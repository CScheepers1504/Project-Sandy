#pragma once

// =============================================================================
// MS5837-30BA — Depth / Pressure / Temperature Sensor
// Library: BlueRobotics MS5837 Library  (install via Library Manager)
//   https://github.com/bluerobotics/BlueRobotics_MS5837_Library
//
// Wiring (I2C, shared bus with AS7341, fixed address 0x76):
//   SDA → PIN_I2C_SDA  (config.h)
//   SCL → PIN_I2C_SCL  (config.h)
//   VIN → 3.3 V,  GND → GND
//
// Calibration:
//   Surface pressure — hold sensor at the water surface, call calibrateSurface().
//   All subsequent depth readings are referenced to that baseline.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <MS5837.h>
#include "config.h"

// ── SENSOR-SPECIFIC SETTINGS (edit here) ─────────────────────────────────────

// Water density (kg/m³):  997 fresh water  |  1025 seawater
#define MS5837_FLUID_DENSITY    1025.0f

// Surface calibration: number of pressure samples to average for the baseline
#define MS5837_SURFACE_CAL_SAMPLES  20

// Minimum warm-up / settling time after begin() (ms)
#define MS5837_MIN_WARMUP_MS        1000UL

// ── DATA STRUCTURE ────────────────────────────────────────────────────────────

struct MS5837Data {
    float pressure_mbar;    // Absolute pressure in mbar
    float temperature_C;    // Sensor temperature in °C (internal, near PCB)
    float depth_m;          // Depth below calibration surface (m)
    bool  valid;
};

// ── CLASS ─────────────────────────────────────────────────────────────────────

class MS5837Sensor {
public:

    // ── Lifecycle ────────────────────────────────────────────────────────────

    bool begin() {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        if (!_dev.init()) {
            Serial.println("[MS5837] init() failed — check SDA/SCL and supply voltage");
            return false;
        }

        _dev.setModel(MS5837::MS5837_30BA);
        _dev.setFluidDensity(MS5837_FLUID_DENSITY);

        _initialized = true;
        Serial.println("[MS5837] Initialized OK");
        return true;
    }

    // ── Calibration ─────────────────────────────────────────────────────────

    // Hold sensor at the water surface (or in air at deployment site), then call.
    // Averages MS5837_SURFACE_CAL_SAMPLES readings for a stable baseline.
    // All depth() values returned after this are relative to this pressure.
    bool calibrateSurface() {
        if (!_initialized) return false;

        Serial.printf("[MS5837] Surface calibration (%d samples)...\n",
                      MS5837_SURFACE_CAL_SAMPLES);

        double sum = 0.0;
        for (int i = 0; i < MS5837_SURFACE_CAL_SAMPLES; i++) {
            _dev.read();
            sum += _dev.pressure();
            delay(100);
        }

        _surfacePressure_mbar = static_cast<float>(sum / MS5837_SURFACE_CAL_SAMPLES);
        _surfaceCalibrated = true;

        Serial.printf("[MS5837] Surface pressure baseline: %.2f mbar\n",
                      _surfacePressure_mbar);
        return true;
    }

    // Override the surface baseline manually (e.g. loaded from NVS).
    void setSurfacePressure(float mbar) {
        _surfacePressure_mbar = mbar;
        _surfaceCalibrated    = true;
    }

    float getSurfacePressure() const { return _surfacePressure_mbar; }

    // ── Reading ──────────────────────────────────────────────────────────────

    MS5837Data read() {
        MS5837Data d = {};

        if (!_initialized) {
            Serial.println("[MS5837] Not initialized");
            return d;
        }

        _dev.read();

        d.pressure_mbar  = _dev.pressure();
        d.temperature_C  = _dev.temperature();

        // Depth = pressure difference from surface converted to metres.
        // ΔP (Pa) / (ρ × g) = depth (m)
        // ΔP (mbar) × 100 → Pa;  g ≈ 9.80665 m/s²
        float dP_Pa = (d.pressure_mbar - _surfacePressure_mbar) * 100.0f;
        d.depth_m   = dP_Pa / (MS5837_FLUID_DENSITY * 9.80665f);

        d.valid = true;
        return d;
    }

    // Returns the average of n readings. Skips failed reads (pressure == 0).
    MS5837Data readAveraged(uint8_t n, uint16_t delayBetweenMs = 100) {
        double sumP = 0, sumT = 0, sumD = 0;
        uint8_t good = 0;

        for (uint8_t i = 0; i < n; i++) {
            MS5837Data r = read();
            if (!r.valid) { delay(delayBetweenMs); continue; }
            sumP += r.pressure_mbar;
            sumT += r.temperature_C;
            sumD += r.depth_m;
            good++;
            if (i < n - 1) delay(delayBetweenMs);
        }

        if (good == 0) return {};

        MS5837Data avg;
        avg.pressure_mbar = static_cast<float>(sumP / good);
        avg.temperature_C = static_cast<float>(sumT / good);
        avg.depth_m       = static_cast<float>(sumD / good);
        avg.valid         = true;
        return avg;
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    bool isInitialized()       const { return _initialized; }
    bool isSurfaceCalibrated() const { return _surfaceCalibrated; }

    void printData(const MS5837Data& d) {
        if (!d.valid) { Serial.println("[MS5837] Invalid data"); return; }
        Serial.printf("[MS5837] Pressure=%.2f mbar  Temp=%.2f °C  Depth=%.3f m\n",
                      d.pressure_mbar, d.temperature_C, d.depth_m);
        if (!_surfaceCalibrated) {
            Serial.println("[MS5837] Note: depth uncalibrated (no surface baseline set)");
        }
    }

private:
    MS5837 _dev;
    float  _surfacePressure_mbar = 1013.25f;  // ISA standard; overwritten by calibrateSurface()
    bool   _initialized          = false;
    bool   _surfaceCalibrated    = false;
};
