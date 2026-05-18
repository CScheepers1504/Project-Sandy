#pragma once

// =============================================================================
// AS7341 — 11-Channel Spectral Sensor
// Library: Adafruit AS7341  (install via Library Manager)
//
// Wiring (I2C, shared bus with MS5837):
//   SDA → PIN_I2C_SDA  (config.h)
//   SCL → PIN_I2C_SCL  (config.h)
//   VIN → 3.3 V,  GND → GND
//
// Calibration:
//   Dark current — block the aperture completely, call calibrateDark().
//   The measured per-channel offset is subtracted from every subsequent read().
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AS7341.h>
#include "config.h"

// ── SENSOR-SPECIFIC SETTINGS (edit here) ─────────────────────────────────────

// Integration time = (ATIME + 1) × (ASTEP + 1) × 2.78 µs
//   ATIME=29, ASTEP=599  → ~50 ms   (turbid / high-scatter water)
//   ATIME=100, ASTEP=999 → ~280 ms  (clear water / low signal)
#define AS7341_ATIME            29
#define AS7341_ASTEP            599

// Gain: AS7341_GAIN_0_5X … AS7341_GAIN_512X
// Reduce gain if any channel reads >60 000 counts (saturation).
#define AS7341_GAIN             AS7341_GAIN_256X

// Samples averaged for dark-current calibration pass
#define AS7341_DARK_CAL_SAMPLES 10

// Minimum time after power-on before readings are stable
#define AS7341_MIN_WARMUP_MS    500UL

// ── DATA STRUCTURE ────────────────────────────────────────────────────────────

struct AS7341Data {
    uint16_t F1_415nm;
    uint16_t F2_445nm;
    uint16_t F3_480nm;
    uint16_t F4_515nm;
    uint16_t F5_555nm;
    uint16_t F6_590nm;
    uint16_t F7_630nm;
    uint16_t F8_680nm;
    uint16_t Clear;
    uint16_t NIR;
    bool     valid;
};

// ── CLASS ─────────────────────────────────────────────────────────────────────

class AS7341Sensor {
public:

    // ── Lifecycle ────────────────────────────────────────────────────────────

    bool begin() {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        if (!_dev.begin()) {
            Serial.println("[AS7341] begin() failed — check SDA/SCL and 3.3V supply");
            return false;
        }

        _dev.setATIME(AS7341_ATIME);
        _dev.setASTEP(AS7341_ASTEP);
        _dev.setGain(AS7341_GAIN);

        _initialized = true;
        Serial.println("[AS7341] Initialized OK");
        return true;
    }

    // ── Calibration ─────────────────────────────────────────────────────────

    // Block the aperture completely (no light), then call this.
    // Stores the per-channel dark current; subtracted from every read() call.
    bool calibrateDark() {
        if (!_initialized) return false;

        Serial.println("[AS7341] Dark calibration — keep aperture blocked...");
        uint32_t acc[12] = {};
        uint16_t raw[12];

        for (int s = 0; s < AS7341_DARK_CAL_SAMPLES; s++) {
            if (!_dev.readAllChannels(raw)) {
                Serial.println("[AS7341] Read error during dark calibration");
                return false;
            }
            for (int i = 0; i < 12; i++) acc[i] += raw[i];
            delay(50);
        }

        for (int i = 0; i < 12; i++) {
            _dark[i] = static_cast<uint16_t>(acc[i] / AS7341_DARK_CAL_SAMPLES);
        }

        _darkCalibrated = true;
        Serial.printf("[AS7341] Dark cal done. Clear=%u  NIR=%u\n",
                      _dark[AS7341_CHANNEL_CLEAR], _dark[AS7341_CHANNEL_NIR]);
        return true;
    }

    // ── Reading ──────────────────────────────────────────────────────────────

    AS7341Data read() {
        AS7341Data d = {};

        if (!_initialized) {
            Serial.println("[AS7341] Not initialized");
            return d;
        }

        uint16_t raw[12];
        if (!_dev.readAllChannels(raw)) {
            Serial.println("[AS7341] readAllChannels() failed");
            return d;
        }

        // readAllChannels() fills the buffer in enum order (as7341_color_channel_t).
        // Dark subtraction is saturating (clamps to 0, never wraps negative).
        d.F1_415nm = _sub(raw, AS7341_CHANNEL_415nm_F1);
        d.F2_445nm = _sub(raw, AS7341_CHANNEL_445nm_F2);
        d.F3_480nm = _sub(raw, AS7341_CHANNEL_480nm_F3);
        d.F4_515nm = _sub(raw, AS7341_CHANNEL_515nm_F4);
        d.F5_555nm = _sub(raw, AS7341_CHANNEL_555nm_F5);
        d.F6_590nm = _sub(raw, AS7341_CHANNEL_590nm_F6);
        d.F7_630nm = _sub(raw, AS7341_CHANNEL_630nm_F7);
        d.F8_680nm = _sub(raw, AS7341_CHANNEL_680nm_F8);
        d.Clear    = _sub(raw, AS7341_CHANNEL_CLEAR);
        d.NIR      = _sub(raw, AS7341_CHANNEL_NIR);
        d.valid    = true;

        return d;
    }

    // Returns the average of n readings. Skips failed reads.
    AS7341Data readAveraged(uint8_t n, uint16_t delayBetweenMs = 50) {
        uint32_t F1=0,F2=0,F3=0,F4=0,F5=0,F6=0,F7=0,F8=0,Cl=0,NI=0;
        uint8_t good = 0;

        for (uint8_t i = 0; i < n; i++) {
            AS7341Data r = read();
            if (!r.valid) { delay(delayBetweenMs); continue; }
            F1 += r.F1_415nm; F2 += r.F2_445nm;
            F3 += r.F3_480nm; F4 += r.F4_515nm;
            F5 += r.F5_555nm; F6 += r.F6_590nm;
            F7 += r.F7_630nm; F8 += r.F8_680nm;
            Cl += r.Clear;    NI += r.NIR;
            good++;
            if (i < n - 1) delay(delayBetweenMs);
        }

        if (good == 0) return {};

        AS7341Data avg;
        avg.F1_415nm = F1/good; avg.F2_445nm = F2/good;
        avg.F3_480nm = F3/good; avg.F4_515nm = F4/good;
        avg.F5_555nm = F5/good; avg.F6_590nm = F6/good;
        avg.F7_630nm = F7/good; avg.F8_680nm = F8/good;
        avg.Clear    = Cl/good; avg.NIR      = NI/good;
        avg.valid    = true;
        return avg;
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────

    bool isInitialized()    const { return _initialized; }
    bool isDarkCalibrated() const { return _darkCalibrated; }
    uint16_t getDark(uint8_t idx) const { return (idx < 12) ? _dark[idx] : 0; }

    void printData(const AS7341Data& d) {
        if (!d.valid) { Serial.println("[AS7341] Invalid data"); return; }
        Serial.printf("[AS7341] F1(415)=%5u  F2(445)=%5u  F3(480)=%5u  F4(515)=%5u\n",
                      d.F1_415nm, d.F2_445nm, d.F3_480nm, d.F4_515nm);
        Serial.printf("         F5(555)=%5u  F6(590)=%5u  F7(630)=%5u  F8(680)=%5u\n",
                      d.F5_555nm, d.F6_590nm, d.F7_630nm, d.F8_680nm);
        Serial.printf("         Clear  =%5u  NIR    =%5u\n", d.Clear, d.NIR);
    }

    // Prints a warning if any spectral channel is near saturation (>threshold).
    void checkSaturation(const AS7341Data& d, uint16_t threshold = 60000) {
        if (!d.valid) return;
        if (d.F1_415nm > threshold || d.F2_445nm > threshold ||
            d.F3_480nm > threshold || d.F4_515nm > threshold ||
            d.F5_555nm > threshold || d.F6_590nm > threshold ||
            d.F7_630nm > threshold || d.F8_680nm > threshold ||
            d.Clear    > threshold || d.NIR      > threshold) {
            Serial.println("[AS7341] WARNING: channel near saturation — reduce gain or ATIME");
        }
    }

private:
    Adafruit_AS7341 _dev;
    uint16_t        _dark[12]       = {};
    bool            _initialized    = false;
    bool            _darkCalibrated = false;

    // Saturating subtraction: raw[ch] - _dark[ch], clamped to zero.
    uint16_t _sub(const uint16_t* raw, as7341_color_channel_t ch) const {
        uint16_t r = raw[ch];
        uint16_t d = _dark[ch];
        return (r > d) ? (r - d) : 0;
    }
};
