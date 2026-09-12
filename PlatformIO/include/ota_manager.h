#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

/*
  ota_manager — universal ESP32 over-the-air update module
  --------------------------------------------------------
  Drop-in, project-agnostic OTA front-end for the Forbes Automotive ESP32
  projects (MFSW controller, Can2Cluster, OpenHaldex, etc.) so every project
  updates the same way. Companion to power_manager and wifi_manager.

  Two independent update targets
  ------------------------------
  ESP32 firmware is split across two flash partitions, so there are two kinds
  of OTA update:

    1. FIRMWARE (application)  -> the compiled program        (firmware.bin)
       Written to the inactive OTA app partition (U_FLASH). This is the code
       that runs on the chip.

    2. FILESYSTEM (web UI)     -> the LittleFS/SPIFFS image    (littlefs.bin)
       Written to the filesystem partition (U_SPIFFS). This is index.html,
       app.js, style.css and any other web assets served to the browser.

  Keeping them separate means you can push a UI-only change without reflashing
  the application, or update the application without disturbing stored web
  assets. The device reboots automatically after either completes.

  HTTP endpoints registered by otaManagerAttach()
  -----------------------------------------------
    GET  /api/ota/info   -> { "version", "board", "hardware" } for the UI.
    POST /api/ota        -> multipart firmware  (.bin) upload -> U_FLASH.
    POST /api/ota/fs     -> multipart filesystem (.bin) upload -> U_SPIFFS.

  The matching web UI lives on the "OTA" tab of the project's index.html and is
  driven by the shared app.js OTA block (type selector + progress bar).

  How to use (any project)
  ------------------------
    #include "ota_manager.h"

    // In setup(), after wifiManagerInit() and BEFORE server.begin():
    ota_config_t ocfg = otaDefaultConfig();
    ocfg.fwVersion = FW_VERSION;   // reported by /api/ota/info
    otaManagerInit(&ocfg);
    otaManagerAttach(server);      // register the three OTA routes
    // ... register your own /api routes ...
    server.begin();

  power_manager integration
  -------------------------
  Override powerIsBusy() to keep the device awake while an upload is running so
  the radio is not dropped mid-transfer:
      bool powerIsBusy() {
        return WiFi.softAPgetStationNum() > 0 || otaInProgress();
      }

  Plain C++/Arduino; depends only on Update.h and ESPAsyncWebServer (already
  used by these projects). No ArduinoJson dependency.
*/

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// ---- Configuration ----------------------------------------------------------
typedef struct
{
  const char *fwVersion;   // version string reported by /api/ota/info (NULL/"" = "")
  uint32_t rebootDelayMs;  // wait after a successful update before ESP.restart() (default 1500)
  bool verbose;            // Serial.printf status lines
} ota_config_t;

// Sensible defaults: no version string, 1.5 s reboot delay, quiet.
ota_config_t otaDefaultConfig(void);

// Store the config. Safe to call once from setup(). Does not touch the server.
void otaManagerInit(const ota_config_t *cfg);

// Register the /api/ota, /api/ota/fs and /api/ota/info routes on the given
// server. Call BEFORE server.begin(). If otaManagerInit() was not called first,
// defaults are used.
void otaManagerAttach(AsyncWebServer &server);

// True while a firmware or filesystem upload is in progress. Use it in
// powerIsBusy() so the radio is not dropped mid-update.
bool otaInProgress(void);

// The firmware version string passed in the config (never NULL; "" if unset).
const char *otaManagerFwVersion(void);

// ---- Weak hooks: override in YOUR project to customise -----------------------

// Called when an upload begins. filesystem = true for /api/ota/fs (U_SPIFFS),
// false for /api/ota (U_FLASH). Default: no-op.
void otaOnStart(bool filesystem);

// Called when an upload finishes, just before the (successful) reboot is
// scheduled. success reflects whether the write completed without error.
// Default: no-op.
void otaOnEnd(bool filesystem, bool success);

#endif // OTA_MANAGER_H
