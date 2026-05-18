#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------
//  Sensor interface.
//
//  These functions are placeholders so this subsystem can be flashed and
//  tested end-to-end (CSV write + Wi-Fi retrieval) before Chris's sensor
//  board is integrated. Replace the bodies of initSensors() and
//  readSensorsOnce() with the real I²C / SPI / ADC reads.
//
//  The DSP layer is decoupled from the hardware — as long as
//  readSensorsOnce() returns a populated RawSample, nothing else changes.
// ---------------------------------------------------------------------

struct RawSample {
  float temp_c;        // °C, water temperature
  float fluorescence;  // raw counts or µg/L chlorophyll-a
  float backscatter;   // raw counts
  float par;           // µmol s⁻¹ m⁻² visible-light spectrum
  float depth_m;       // metres below surface (from pressure sensor)
  float battery_v;     // pack voltage
};

inline void initSensors() {
  // TODO (Chris): initialise I²C bus, ADC channels, sensor power rails.
  // Called once per wake — keep it fast to preserve battery.
  randomSeed(esp_random());
}

inline RawSample readSensorsOnce() {
  RawSample s;
  // -----------------------------------------------------------------
  // PLACEHOLDER: realistic-ish noisy values around Southern Ocean
  // surface conditions. Strip and replace with real reads.
  // -----------------------------------------------------------------
  s.temp_c       = 4.5f  + (random(-50, 50)   / 100.0f);
  s.fluorescence = 1200  + random(-150, 150);
  s.backscatter  = 350   + random(-40, 40);
  s.par          = 280   + random(-60, 60);
  s.depth_m      = 75.0f + (random(-200, 200) / 100.0f);
  s.battery_v    = 6.20f + (random(-10, 10)   / 100.0f);

  // Occasional spike — to exercise the trimmed-mean filter during tests.
  if (random(0, 50) == 0) s.fluorescence += 5000;

  return s;
}
