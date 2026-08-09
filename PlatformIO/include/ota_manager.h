#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

/*
  ota_manager — universal ESP32 over-the-air update module
  --------------------------------------------------------
  OTA backend for any ESP32 (DevKit V1 / WROOM-32 /
  C3 / etc.) build that already runs an ESPAsyncWebServer.  Designed to be used across all projects,
  it mirrors the SpeedPulser screen layout exactly: a single endpoint that flashes either the
  application firmware or the LittleFS web-UI image, selected by ?mode=.

  Endpoints:
    POST /api/ota-update?mode=firmware      -> writes the app partition  (U_FLASH)
    POST /api/ota-update?mode=filesystem    -> writes the LittleFS image (U_SPIFFS)
    GET  /api/version                       -> { version, hardware, board }

  The matching web-UI card lives in each project's data/ (index.html + app.js). 
  The browser posts the .bin as multipart form data to /api/ota-update?mode=... and reads the JSON
  reply { "status":"ok"|"error", "message":"..." }.

  How to use (any project with an AsyncWebServer instance called `server`)
  -----------------------------------------------------------------------
    #include "ota_manager.h"

    // In your web-server setup, AFTER creating routes but BEFORE server.begin():
    OtaInfo info;
    info.version  = FW_VERSION;              // your version string
    info.hardware = "ESP32";                 // or "ESP32-C3", etc.
    info.board    = "DOIT ESP32 DEVKIT V1";  // human-readable board name
    otaBegin(server, info, false);           // last arg = verbose logging

    server.begin();

  Notes
  -----
  - Requires the partition table to have two app slots (default.csv, min_spiffs.csv,
    etc.). Single-app tables cannot OTA the firmware.
  - On a successful flash the device reboots from a short FreeRTOS task so the
    async TCP stack can flush the HTTP response first (a blocking delay() in the
    request handler would stall ESPAsyncWebServer and drop the reply).
  - Dependencies are ESPAsyncWebServer and the Arduino Update library
*/

#include <ESPAsyncWebServer.h>

// Firmware/hardware identification returned by GET /api/version.
struct OtaInfo
{
  const char *version = "";       // firmware version string
  const char *hardware = "ESP32"; // chip family, e.g. "ESP32" / "ESP32-C3"
  const char *board = "";         // board name as human-readable string, e.g. "LOLIN C3 Mini"
};

// Register POST /api/ota-update (firmware + filesystem via ?mode=) on `server`.
// `verbose` streams progress/errors to Serial.
void otaRegisterUpdate(AsyncWebServer &server, bool verbose = false);

// Register GET /api/version returning identification.
void otaRegisterVersion(AsyncWebServer &server, const OtaInfo &info);

// Register both the update and version endpoints.
void otaBegin(AsyncWebServer &server, const OtaInfo &info, bool verbose = false);

#endif // OTA_MANAGER_H
