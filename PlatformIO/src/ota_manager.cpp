/*
  ota_manager — universal ESP32 over-the-air update module (implementation)
  See ota_manager.h for the full rationale and usage notes.

  Firmware vs filesystem
  ----------------------
  Both targets share the exact same streaming upload logic; only the Update
  command differs:
    - U_FLASH  (default) writes the application to the inactive OTA app slot.
    - U_SPIFFS writes the LittleFS/SPIFFS image to the filesystem partition.
  The Arduino Update library selects the right partition for each command, so
  the same handler serves both once the command is captured per-route.
*/

#include "ota_manager.h"

#include <Update.h>
#include <esp_chip_info.h>

// ---- Module state -----------------------------------------------------------
static ota_config_t g_cfg;
static bool g_initialised = false;
static volatile bool g_inProgress = false;
static volatile bool g_lastWasFilesystem = false;

// ---- Weak default hooks (override in your project) --------------------------
__attribute__((weak)) void otaOnStart(bool filesystem) { (void)filesystem; }
__attribute__((weak)) void otaOnEnd(bool filesystem, bool success)
{
  (void)filesystem;
  (void)success;
}

// ---- Helpers ----------------------------------------------------------------
static void otaLog(const char *fmt, ...)
{
  if (!g_cfg.verbose)
    return;
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("[OTA] %s\n", buf);
}

static bool strHasContent(const char *s) { return s != nullptr && s[0] != '\0'; }

// Reboot on a short-lived task so the HTTP response can flush to the browser
// before the radio drops. Runs once then deletes itself.
static void otaScheduleReboot(void)
{
  static uint32_t delayMs;
  delayMs = g_cfg.rebootDelayMs;
  xTaskCreate(
      [](void *arg)
      {
        uint32_t d = *static_cast<uint32_t *>(arg);
        vTaskDelay(pdMS_TO_TICKS(d));
        ESP.restart();
        vTaskDelete(nullptr);
      },
      "ota_reboot", 2048, &delayMs, 1, nullptr);
}

// Streaming upload handler shared by both routes. `filesystem` picks the Update
// command (U_SPIFFS vs U_FLASH) and is reported to the otaOnStart/End hooks.
static void otaHandleUpload(const String &filename, size_t index, uint8_t *data,
                            size_t len, bool final, bool filesystem)
{
  if (index == 0)
  {
    g_inProgress = true;
    g_lastWasFilesystem = filesystem;
    otaLog("Starting %s OTA: %s", filesystem ? "filesystem" : "firmware", filename.c_str());
    otaOnStart(filesystem);
    const int cmd = filesystem ? U_SPIFFS : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd))
    {
      if (g_cfg.verbose)
        Update.printError(Serial);
    }
  }

  if (len > 0 && !Update.hasError())
  {
    if (Update.write(data, len) != len)
    {
      if (g_cfg.verbose)
        Update.printError(Serial);
    }
  }

  if (final)
  {
    const bool success = !Update.hasError() && Update.end(true);
    if (!success && g_cfg.verbose)
      Update.printError(Serial);
    otaLog("%s OTA %s, %u bytes", filesystem ? "filesystem" : "firmware",
           success ? "complete" : "FAILED", static_cast<unsigned>(index + len));
    otaOnEnd(filesystem, success);
    g_inProgress = false;
  }
}

// Completion handler (fires after the whole multipart body is received).
static void otaHandleComplete(AsyncWebServerRequest *request)
{
  const bool success = !Update.hasError();
  const bool fs = g_lastWasFilesystem;
  const char *okMsg = fs
                          ? "{\"ok\":true,\"success\":true,\"message\":\"Filesystem update complete.\"}"
                          : "{\"ok\":true,\"success\":true,\"message\":\"Update complete. Rebooting...\"}";
  request->send(success ? 200 : 500, "application/json",
                success ? okMsg
                        : "{\"ok\":false,\"success\":false,\"message\":\"Update failed\"}");
  // Only firmware updates reboot; filesystem uploads keep the device running so
  // the user can immediately follow up with the firmware image.
  if (success && !fs)
    otaScheduleReboot();
}

// ---- Public API -------------------------------------------------------------
ota_config_t otaDefaultConfig(void)
{
  ota_config_t c = {};
  c.fwVersion = nullptr;
  c.rebootDelayMs = 1500;
  c.verbose = false;
  return c;
}

void otaManagerInit(const ota_config_t *cfg)
{
  g_cfg = cfg ? *cfg : otaDefaultConfig();
  g_initialised = true;
}

void otaManagerAttach(AsyncWebServer &server)
{
  if (!g_initialised)
    otaManagerInit(nullptr);

  // Device/firmware info for the OTA tab.
  server.on("/api/ota/info", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    char board[64];
    snprintf(board, sizeof(board), "ESP32 (%d cores, Rev.%d)", chip.cores, chip.revision);
    char hardware[48];
    snprintf(hardware, sizeof(hardware), "ESP32 Revision %d", chip.revision);

    String json = "{";
    json += "\"version\":\"";  json += otaManagerFwVersion(); json += "\",";
    json += "\"board\":\"";    json += board;                 json += "\",";
    json += "\"hardware\":\""; json += hardware;              json += "\"}";

    AsyncWebServerResponse *res = request->beginResponse(200, "application/json", json);
    res->addHeader("Cache-Control", "no-store");
    request->send(res); });

  // Firmware (application) — U_FLASH.
  server.on(
      "/api/ota", HTTP_POST, otaHandleComplete,
      [](AsyncWebServerRequest *request, const String &filename, size_t index,
         uint8_t *data, size_t len, bool final)
      {
        (void)request;
        otaHandleUpload(filename, index, data, len, final, /*filesystem=*/false);
      });

  // Filesystem (web UI) — U_SPIFFS.
  server.on(
      "/api/ota/fs", HTTP_POST, otaHandleComplete,
      [](AsyncWebServerRequest *request, const String &filename, size_t index,
         uint8_t *data, size_t len, bool final)
      {
        (void)request;
        otaHandleUpload(filename, index, data, len, final, /*filesystem=*/true);
      });

  otaLog("routes attached (/api/ota, /api/ota/fs, /api/ota/info)");
}

bool otaInProgress(void) { return g_inProgress; }

const char *otaManagerFwVersion(void)
{
  return strHasContent(g_cfg.fwVersion) ? g_cfg.fwVersion : "";
}
