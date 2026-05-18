#pragma once
#include <Arduino.h>
#include <algorithm>
#include "config.h"
#include "sensors.h"

// ---------------------------------------------------------------------
//  Signal processing pipeline
//  ------------------------
//  Per reading:
//    1. Take N raw samples spaced ~50 ms apart  (≈ 1 s burst).
//    2. Sort each channel, drop the top & bottom TRIM_FRACTION,
//       average the remainder.
//
//  Why trimmed mean and not a Kalman / IIR EMA across hours?
//    - Robust against single-sample spikes (plankton drift past the
//      fluorometer window, ADC glitches, motor EMI).
//    - Stateless — no RTC-backed history to manage across deep sleeps.
//    - Doesn't smear genuine inter-hour change (which we *want* to see
//      during a depth-profile transit).
// ---------------------------------------------------------------------

struct Reading {
  time_t epoch;
  float temp_c;
  float pressure_kpa;
  float f1;
  float f2;
  float f3;
  float f4;
  float f5;
  float f6;
  float f7;
  float f8;
  float tia_voltage;
  float depth_m;
  float battery_v;
  float fluorescence;
  float backscatter;
  float par;
};

static inline float trimmedMean(float* buf, int n, float trimFrac) {
  std::sort(buf, buf + n);
  int trim = (int)(n * trimFrac);
  if (trim * 2 >= n) trim = 0;          // safety: never trim everything
  float sum = 0.0f;
  int   cnt = 0;
  for (int i = trim; i < n - trim; ++i) { sum += buf[i]; ++cnt; }
  return cnt ? sum / cnt : 0.0f;
}

inline Reading takeReadingWithDSP() {
  float t[OVERSAMPLE_COUNT];
  float f[OVERSAMPLE_COUNT];
  float b[OVERSAMPLE_COUNT];
  float p[OVERSAMPLE_COUNT];
  float d[OVERSAMPLE_COUNT];
  float v[OVERSAMPLE_COUNT];

  initSensors();
  for (int i = 0; i < OVERSAMPLE_COUNT; ++i) {
    RawSample s = readSensorsOnce();
    t[i] = s.temp_c;
    f[i] = s.fluorescence;
    b[i] = s.backscatter;
    p[i] = s.par;
    d[i] = s.depth_m;
    v[i] = s.battery_v;
    delay(SAMPLE_SPACING_MS);
  }

  Reading r;
  r.temp_c       = trimmedMean(t, OVERSAMPLE_COUNT, TRIM_FRACTION);
  r.fluorescence = trimmedMean(f, OVERSAMPLE_COUNT, TRIM_FRACTION);
  r.backscatter  = trimmedMean(b, OVERSAMPLE_COUNT, TRIM_FRACTION);
  r.par          = trimmedMean(p, OVERSAMPLE_COUNT, TRIM_FRACTION);
  r.depth_m      = trimmedMean(d, OVERSAMPLE_COUNT, TRIM_FRACTION);
  r.battery_v    = trimmedMean(v, OVERSAMPLE_COUNT, TRIM_FRACTION);
  return r;
}
