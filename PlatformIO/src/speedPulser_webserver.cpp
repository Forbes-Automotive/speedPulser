#include "speedPulser_defs.h"
#include "speedPulser_webserver.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include <Update.h>

extern AsyncWebServer server;

/**
 * Initialize AsyncWebServer with API endpoints and static file serving
 */
void setupWebServer() {
  // Always bring up API endpoints, even if static FS is unavailable.
  bool littleFsMounted = LittleFS.begin(false);
  if (!littleFsMounted) {
    DEBUG_PRINTLN("LittleFS mount failed; attempting format + remount...");
    littleFsMounted = LittleFS.begin(true);
    if (littleFsMounted) {
      DEBUG_PRINTLN("LittleFS remounted after format");
    } else {
      DEBUG_PRINTLN("LittleFS remount failed");
    }
  }

  if (littleFsMounted) {
    if (!LittleFS.exists("/index.html")) {
      DEBUG_PRINTLN("LittleFS mounted but /index.html is missing");
    }

    // Serve static files from filesystem image
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.serveStatic("/app.js", LittleFS, "/app.js");
    server.serveStatic("/style.css", LittleFS, "/style.css");
  } else {
    // Fallback root for diagnosing filesystem flashing issues.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(500, "text/plain",
                    "LittleFS not available. Firmware is running, but web assets are missing or FS mount failed.");
    });
  }

  // GET /api/settings - Return current settings
  server.on("/api/settings", HTTP_GET, handleGetSettings);

  // GET /api/calibrations - Return calibration option list
  server.on("/api/calibrations", HTTP_GET, handleGetCalibrations);

  // GET /api/status - Return live sensor data
  server.on("/api/status", HTTP_GET, handleGetStatus);

  // GET /api/test-status - Return minimal live test-speed data
  server.on("/api/test-status", HTTP_GET, handleGetTestStatus);

  // GET /api/version - Return firmware version
  server.on("/api/version", HTTP_GET, handleGetVersion);

  // POST /api/control - Update settings
  server.on("/api/control", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},  // empty onRequest
    nullptr,                                  // no upload handler
    handlePostControl);                       // onBody callback

  // POST /api/action - Execute actions
  server.on("/api/action", HTTP_POST,
    [](AsyncWebServerRequest *request) {},  // empty onRequest
    nullptr,                                  // no upload handler
    handlePostAction);                        // onBody callback

  // POST /api/ota-update - OTA firmware upload
  server.on("/api/ota-update", HTTP_POST,
    // onRequest: send final response after upload completes
    [](AsyncWebServerRequest *request) {
      bool success = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(
        200, "application/json",
        success ? "{\"status\":\"ok\",\"message\":\"Update complete. Rebooting...\"}" 
                : "{\"status\":\"error\",\"message\":\"Update failed\"}"
      );
      response->addHeader("Connection", "close");
      request->send(response);
      if (success) {
        delay(500);
        ESP.restart();
      }
    },
    // onUpload: stream incoming binary to Update
    [](AsyncWebServerRequest *request, const String &filename,
       size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        DEBUG_PRINTF("OTA update starting: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          DEBUG_PRINTF("OTA begin failed: %s\n", Update.errorString());
        }
      }
      if (Update.isRunning()) {
        if (Update.write(data, len) != len) {
          DEBUG_PRINTF("OTA write failed: %s\n", Update.errorString());
        }
      }
      if (final) {
        if (Update.end(true)) {
          DEBUG_PRINTF("OTA complete: %u bytes\n", index + len);
        } else {
          DEBUG_PRINTF("OTA end failed: %s\n", Update.errorString());
        }
      }
    }
  );

  // Start server
  server.begin();
  DEBUG_PRINTLN("Web server started");
}

/**
 * GET /api/settings - Return all current settings as JSON
 */
void handleGetSettings(AsyncWebServerRequest *request) {
  JsonDocument doc;

  // Basic settings
  doc["hasNeedleSweep"] = hasNeedleSweep;
  doc["sweepSpeed"] = sweepSpeed;
  doc["motorPerformanceVal"] = motorPerformanceVal;
  doc["calibrationText"] = getCalibrationText(motorPerformanceVal);
  doc["maxSpeed"] = maxSpeed;
  doc["maxFreqHall"] = maxFreqHall;
  doc["speedOffset"] = speedOffset;
  doc["speedOffsetPositive"] = speedOffsetPositive;
  doc["convertToMPH"] = convertToMPH;
  doc["useSpeedOffsetCurve"] = useSpeedOffsetCurve;
  JsonArray speedCurveOffsets = doc["speedOffsetCurveOffsets"].to<JsonArray>();
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++) {
    speedCurveOffsets.add(speedOffsetCurveOffsets[i]);
  }
  doc["averageFilter"] = averageFilter;

  // Test mode settings
  doc["testSpeedo"] = testSpeedo;
  doc["testCal"] = testCal;
  doc["tempDutyCycle"] = tempDutyCycle;

  // Version info
  doc["fwVersion"] = VERSION;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * GET /api/calibrations - Return calibration options
 */
void handleGetCalibrations(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray calibrations = doc["calibrations"].to<JsonArray>();

  const uint8_t count = getCalibrationCount();
  for (uint8_t i = 1; i <= count; i++) {
    JsonObject item = calibrations.add<JsonObject>();
    item["id"] = i;
    item["name"] = getCalibrationText(i);
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * GET /api/status - Return live speed/motor data
 */
void handleGetStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;

  // Live motor data
  doc["dutyCycle"] = dutyCycle;
  doc["appliedDutyCycle"] = appliedDutyCycle;
  doc["dutyCycleIncoming"] = dutyCycleIncoming;
  doc["motorPerformanceVal"] = motorPerformanceVal;
  doc["calibrationText"] = getCalibrationText(motorPerformanceVal);
  doc["rawCount"] = rawCount;
  doc["ledCounter"] = ledCounter;
  doc["tempDutyCycle"] = tempDutyCycle;
  doc["tempSpeed"] = tempSpeed;
  doc["testSpeedo"] = testSpeedo;
  doc["testNeedleSweep"] = testNeedleSweep;
  doc["speedOffsetType"] = useSpeedOffsetCurve ? "Curve" : "Global";
  doc["currentSpeedOffset"] = currentSpeedOffset;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * GET /api/test-status - Return minimal live test-speed data
 */
void handleGetTestStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;

  doc["testSpeedo"] = testSpeedo;
  doc["tempSpeed"] = tempSpeed;
  doc["appliedDutyCycle"] = appliedDutyCycle;
  doc["motorPerformanceVal"] = motorPerformanceVal;
  doc["calibrationText"] = getCalibrationText(motorPerformanceVal);
  doc["speedOffsetType"] = useSpeedOffsetCurve ? "Curve" : "Global";
  doc["currentSpeedOffset"] = currentSpeedOffset;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * GET /api/version - Return firmware and hardware version
 */
void handleGetVersion(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["version"] = "2.00";
  doc["hardware"] = "ESP32-C3";
  doc["board"] = "LOLIN C3 Mini";

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * POST /api/control - Update a setting value
 * Expected JSON: { "key": "settingName", "value": settingValue }
 */
void handlePostControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index + len != total) {
    return; // Wait for complete payload
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* key = doc["key"];
  JsonVariant value = doc["value"];

  if (!key) {
    request->send(400, "application/json", "{\"error\":\"Missing key\"}");
    return;
  }

  // Update settings based on key
  if (strcmp(key, "hasNeedleSweep") == 0) {
    hasNeedleSweep = value.as<bool>();
  } else if (strcmp(key, "sweepSpeed") == 0) {
    sweepSpeed = value.as<uint8_t>();
  } else if (strcmp(key, "motorCalSelection") == 0) {
    motorPerformanceVal = value.as<uint8_t>();
    updateMotorPerformance = true;
    updateMotorArray();
  } else if (strcmp(key, "maxSpeed") == 0) {
    maxSpeed = value.as<uint16_t>();
  } else if (strcmp(key, "maxFreqHall") == 0) {
    maxFreqHall = value.as<uint16_t>();
  } else if (strcmp(key, "speedOffset") == 0) {
    speedOffset = value.as<uint8_t>();
  } else if (strcmp(key, "speedOffsetPositive") == 0) {
    speedOffsetPositive = value.as<bool>();
  } else if (strcmp(key, "convertToMPH") == 0) {
    convertToMPH = value.as<bool>();
  } else if (strcmp(key, "useSpeedOffsetCurve") == 0) {
    useSpeedOffsetCurve = value.as<bool>();
  } else if (strcmp(key, "curveOffset0") == 0) {
    speedOffsetCurveOffsets[0] = value.as<int16_t>();
  } else if (strcmp(key, "curveOffset1") == 0) {
    speedOffsetCurveOffsets[1] = value.as<int16_t>();
  } else if (strcmp(key, "curveOffset2") == 0) {
    speedOffsetCurveOffsets[2] = value.as<int16_t>();
  } else if (strcmp(key, "curveOffset3") == 0) {
    speedOffsetCurveOffsets[3] = value.as<int16_t>();
  } else if (strcmp(key, "curveOffset4") == 0) {
    speedOffsetCurveOffsets[4] = value.as<int16_t>();
  } else if (strcmp(key, "testSpeedo") == 0) {
    testSpeedo = value.as<bool>();
    if (!testSpeedo) {
      tempSpeed = 0;
      dutyCycle = 0;
      appliedDutyCycle = 0;
      ledcWrite(LEDC_CHANNEL_MOTOR, 0);
    }
  } else if (strcmp(key, "tempSpeed") == 0) {
    tempSpeed = value.as<uint16_t>();
    if (testSpeedo) {
      testSpeed();  // apply immediately, don't wait for next task cycle
    }
  } else if (strcmp(key, "testCal") == 0) {
    testCal = value.as<bool>();
  } else if (strcmp(key, "averageFilter") == 0) {
    uint8_t newVal = value.as<uint8_t>();
    if (newVal < 1) newVal = 1;
    if (newVal > 10) newVal = 10;
    averageFilter = newVal;
    resetMedianFilter();
  } else {
    request->send(400, "application/json", "{\"error\":\"Unknown setting\"}");
    return;
  }

  if (strcmp(key, "curveOffset0") == 0 ||
      strcmp(key, "curveOffset1") == 0 || strcmp(key, "curveOffset2") == 0 ||
      strcmp(key, "curveOffset3") == 0 || strcmp(key, "curveOffset4") == 0) {
    normaliseSpeedOffsetCurve();
  }

  DEBUG_PRINTF("Setting %s = %s\n", key, value.as<String>().c_str());
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

/**
 * POST /api/action - Execute button actions
 * Expected JSON: { "action": "actionName" }
 */
void handlePostAction(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index + len != total) {
    return; // Wait for complete payload
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* action = doc["action"];

  if (!action) {
    request->send(400, "application/json", "{\"error\":\"Missing action\"}");
    return;
  }

  // Execute action
  if (strcmp(action, "needleSweep") == 0) {
    testNeedleSweep = true;
  } else if (strcmp(action, "calPrevious") == 0) {
    if (tempDutyCycle == 0) {
      tempDutyCycle = 385;
    } else {
      tempDutyCycle = tempDutyCycle - 1;
    }
  } else if (strcmp(action, "calNext") == 0) {
    if (tempDutyCycle >= 385) {
      tempDutyCycle = 0;
    } else {
      tempDutyCycle = tempDutyCycle + 1;
    }
  } else {
    request->send(400, "application/json", "{\"error\":\"Unknown action\"}");
    return;
  }

  DEBUG_PRINTF("Action executed: %s\n", action);
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}
