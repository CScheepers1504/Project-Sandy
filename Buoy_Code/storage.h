#pragma once
#include <LittleFS.h>
#include <time.h>
#include "config.h"
#include "dsp.h"

// ---------------------------------------------------------------------
//  CSV format
//  ----------
//  epoch_utc, iso_time, temp_c, fluorescence, backscatter, par, depth_m, battery_v
//
//  - Single file (LOG_FILE) — append-only.
//  - Header written automatically on first ever write.
//  - LittleFS is power-loss safe; a brown-out mid-write leaves the file
//    valid up to the last sync, never corrupted globally.
// ---------------------------------------------------------------------

inline bool logReading(const Reading& r) {
  if (!LittleFS.exists(LOG_DIR)) {
    LittleFS.mkdir(LOG_DIR);
  }

  const bool isNew = !LittleFS.exists(LOG_FILE);
  File f = LittleFS.open(LOG_FILE, isNew ? "w" : "a");
  if (!f) {
    Serial.println("ERR: cannot open log file");
    return false;
  }

  if (isNew) {
    f.println("epoch_utc,iso_time,temp_c,fluorescence,backscatter,par,depth_m,battery_v");
  }

  struct tm tm_info;
  gmtime_r(&r.epoch, &tm_info);
  char iso[24];
  strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

  f.printf("%ld,%s,%.3f,%.2f,%.2f,%.2f,%.3f,%.3f\n",
           (long)r.epoch, iso,
           r.temp_c, r.fluorescence, r.backscatter,
           r.par, r.depth_m, r.battery_v);

  f.flush();
  f.close();
  return true;
}
