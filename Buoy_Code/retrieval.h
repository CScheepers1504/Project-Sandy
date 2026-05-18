#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "config.h"

// ---------------------------------------------------------------------
//  Retrieval portal
//  ----------------
//  Brings up an ESP32 soft-AP. Laptop joins it, opens http://192.168.4.1/,
//  clicks the CSV link → file downloads to laptop. No internet, no
//  cables, no extra hardware.
//
//  Trigger: hold BOOT (GPIO 0) at power-on for ~2 s. Times out after
//  RETRIEVAL_TIMEOUT_MS to avoid burning battery if the user forgets.
// ---------------------------------------------------------------------

static WebServer  server(80);
static bool       _serverStop = false;

static String _indexHtml() {
  File f = LittleFS.open(LOG_FILE, "r");
  size_t bytes = f ? f.size() : 0;
  if (f) f.close();

  String html;
  html.reserve(1024);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>");
  html += DEPLOYMENT_ID;
  html += F(" — Recovery</title><style>"
            "body{font-family:system-ui,-apple-system,sans-serif;max-width:560px;"
            "margin:2rem auto;padding:0 1rem;color:#222}"
            "h1{color:#024c6b}"
            ".card{background:#f4f6f8;border-radius:8px;padding:1rem;margin:1rem 0}"
            "a.btn{display:inline-block;padding:.6rem 1rem;background:#024c6b;"
            "color:#fff;border-radius:6px;text-decoration:none;margin:.3rem 0}"
            "a.btn:hover{background:#036a96}"
            ".meta{color:#666;font-size:.9rem}"
            "code{background:#eee;padding:.1rem .3rem;border-radius:3px}"
            "</style></head><body>");
  html += F("<h1>🌊 Buoy Recovery</h1>");
  html += F("<p class='meta'>Deployment <code>");
  html += DEPLOYMENT_ID;
  html += F("</code></p>");
  html += F("<div class='card'>");
  if (bytes == 0) {
    html += F("<p><em>No data file found.</em></p>");
  } else {
    html += F("<p><strong>buoy.csv</strong><br><span class='meta'>");
    html += String(bytes);
    html += F(" bytes</span></p>");
    html += F("<a class='btn' href='/download'>⬇︎ Download CSV</a>");
  }
  html += F("</div>");
  html += F("<div class='card meta'>"
            "<p>Power-cycle the buoy (or wait 10 min) to leave retrieval "
            "mode and resume logging.</p>"
            "<p><a href='/wipe' onclick='return confirm(\"Delete the CSV?\")'>"
            "Wipe data after download</a></p>"
            "</div>");
  html += F("</body></html>");
  return html;
}

static void _handleRoot()     { server.send(200, "text/html", _indexHtml()); }

static void _handleDownload() {
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) { server.send(404, "text/plain", "No data file"); return; }
  server.sendHeader("Content-Disposition",
                    String("attachment; filename=") + DEPLOYMENT_ID + "_buoy.csv");
  server.streamFile(f, "text/csv");
  f.close();
}

static void _handleWipe() {
  bool ok = LittleFS.remove(LOG_FILE);
  String msg = ok ? "Deleted. <a href='/'>Back</a>"
                  : "Failed to delete. <a href='/'>Back</a>";
  server.send(200, "text/html", "<p>" + msg + "</p>");
}

static void _handle404() { server.send(404, "text/plain", "Not found"); }

inline void enterRetrievalMode() {
  Serial.println("====================================");
  Serial.println("  RETRIEVAL MODE — soft-AP starting ");
  Serial.println("====================================");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(200);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("  SSID:     %s\n", AP_SSID);
  Serial.printf("  Password: %s\n", AP_PASSWORD);
  Serial.printf("  URL:      http://%s/\n", ip.toString().c_str());
  Serial.printf("  Timeout:  %lu s\n", RETRIEVAL_TIMEOUT_MS / 1000UL);

  server.on("/",         _handleRoot);
  server.on("/download", _handleDownload);
  server.on("/wipe",     _handleWipe);
  server.onNotFound(_handle404);
  server.begin();

  const unsigned long start = millis();
  while (millis() - start < RETRIEVAL_TIMEOUT_MS) {
    server.handleClient();
    delay(2);
  }

  Serial.println("Retrieval timeout — back to logging.");
  WiFi.softAPdisconnect(true);
  esp_sleep_enable_timer_wakeup((uint64_t)WAKE_INTERVAL_S * 1000000ULL);
  esp_deep_sleep_start();
}
