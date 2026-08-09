#include "ota_manager.h"
#include <Update.h>
#include <ArduinoJson.h>
#include <stdarg.h>

static OtaInfo s_info;
static bool s_verbose = false;

static void otaLog(const char *fmt, ...)
{
  if (!s_verbose)
  {
    return;
  }
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("[OTA] %s\n", buf);
}

// Reboot from a short-lived task so ESPAsyncWebServer can flush the HTTP reply
// first. A blocking delay() in the request handler would stall the async TCP
// stack and the browser would never see the success message.
static void otaScheduleRestart()
{
  xTaskCreate(
      [](void *)
      {
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP.restart();
        vTaskDelete(nullptr);
      },
      "ota_restart", 2048, nullptr, 1, nullptr);
}

void otaRegisterUpdate(AsyncWebServer &server, bool verbose)
{
  s_verbose = verbose;

  server.on(
      "/api/ota-update", HTTP_POST,
      // onRequest: runs once the whole upload has been received.
      [](AsyncWebServerRequest *request)
      {
        bool success = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            success ? "{\"status\":\"ok\",\"message\":\"Update complete. Rebooting...\"}"
                    : "{\"status\":\"error\",\"message\":\"Update failed\"}");
        response->addHeader("Connection", "close");
        request->send(response);
        if (success)
        {
          otaScheduleRestart();
        }
      },
      // onUpload: stream each chunk of the incoming .bin to the Update partition.
      [](AsyncWebServerRequest *request, const String &filename,
         size_t index, uint8_t *data, size_t len, bool final)
      {
        if (index == 0)
        {
          // ?mode=filesystem targets the LittleFS image; anything else is firmware.
          int otaCmd = U_FLASH;
          if (request->hasParam("mode") &&
              request->getParam("mode")->value() == "filesystem")
          {
            otaCmd = U_SPIFFS; // Update lib uses U_SPIFFS for any FS partition (incl. LittleFS)
          }
          otaLog("start %s (%s)", filename.c_str(),
                 otaCmd == U_SPIFFS ? "filesystem" : "firmware");
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, otaCmd))
          {
            otaLog("begin failed: %s", Update.errorString());
          }
        }

        if (Update.isRunning())
        {
          if (Update.write(data, len) != len)
          {
            otaLog("write failed: %s", Update.errorString());
          }
        }

        if (final)
        {
          if (Update.end(true))
          {
            otaLog("complete: %u bytes", (unsigned)(index + len));
          }
          else
          {
            otaLog("end failed: %s", Update.errorString());
          }
        }
      });
}

void otaRegisterVersion(AsyncWebServer &server, const OtaInfo &info)
{
  s_info = info;

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    JsonDocument doc;
    doc["version"]  = s_info.version;
    doc["hardware"] = s_info.hardware;
    doc["board"]    = s_info.board;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response); });
}

void otaBegin(AsyncWebServer &server, const OtaInfo &info, bool verbose)
{
  otaRegisterUpdate(server, verbose);
  otaRegisterVersion(server, info);
}
