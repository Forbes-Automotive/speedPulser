#include "speedPulser_defs.h"
#include "speedPulser_webserver.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include "speedPulser_calBuilder.h"
#include "ota_manager.h"

extern AsyncWebServer server;

/**
 * Initialize AsyncWebServer with API endpoints and static file serving
 */
void setupWebServer() {
  // Always bring up API endpoints, even if static FS is unavailable.
  bool littleFsMounted = LittleFS.begin(false);
  if (!littleFsMounted) {
    DEBUG_WEB("LittleFS mount failed; attempting format + remount...");
    littleFsMounted = LittleFS.begin(true);
    if (littleFsMounted) {
      DEBUG_WEB("LittleFS remounted after format");
    } else {
      DEBUG_WEB("LittleFS remount failed");
    }
  }

  if (littleFsMounted) {
    if (!LittleFS.exists("/index.html")) {
      DEBUG_WEB("LittleFS mounted but /index.html is missing");
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

  // GET /api/calcurve - Return the active calibration curve (duty vs speed)
  server.on("/api/calcurve", HTTP_GET, handleGetCalCurve);

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

  // GET /api/cal - Return the SpeedPulserV2 custom calibration builder state
  server.on("/api/cal", HTTP_GET, handleGetCal);

  // POST /api/cal - Custom calibration builder operations
  server.on("/api/cal", HTTP_POST,
    [](AsyncWebServerRequest *request) {},  // empty onRequest
    nullptr,                                  // no upload handler
    handlePostCal);                           // onBody callback

  // OTA (firmware + LittleFS web UI) via the shared, project-agnostic module.
  // Registers POST /api/ota-update?mode=firmware|filesystem and GET /api/version.
  OtaInfo otaInfo;
  otaInfo.version = VERSION;
  otaInfo.hardware = "ESP32-C3";
  otaInfo.board = "LOLIN C3 Mini";
  otaBegin(server, otaInfo, (enableDebug && debugWeb));

  // Start server
  server.begin();
  DEBUG_WEB("web server started");
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

  // Direction & feedback (PID)
  doc["reverseDirection"] = reverseDirection;
  doc["feedbackEnable"] = feedbackEnable;
  doc["pidKp"] = pidKp;
  doc["pidKi"] = pidKi;
  doc["pidKd"] = pidKd;
  doc["feedbackDeadband"] = feedbackDeadband;
  doc["feedbackMaxFreq"] = feedbackMaxFreq;
  doc["feedbackMinSpeed"] = feedbackMinSpeed;

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

  // SpeedPulserV2 custom calibration slot — only offered once it has anchors.
  if (customCalCount >= 2) {
    JsonObject item = calibrations.add<JsonObject>();
    item["id"] = CUSTOM_CAL_ID;
    item["name"] = String("\u2605 Custom: ") + customCalName;
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
  doc["incomingSpeed"] = requestedSpeed;
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
  doc["reverseDirection"] = reverseDirection;
  doc["feedbackEnable"] = feedbackEnable;
  doc["measuredSpeed"] = measuredSpeed;
  doc["pidCorrection"] = pidCorrection;
  doc["measuredFreqRawHz"] = measuredFreqRawHz;
  doc["feedbackAvailable"] = feedbackAvailable;
  doc["feedbackMissing"] = feedbackMissing;

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
  doc["measuredSpeed"] = measuredSpeed;
  doc["pidCorrection"] = pidCorrection;
  doc["measuredFreqRawHz"] = measuredFreqRawHz;
  doc["feedbackAvailable"] = feedbackAvailable;
  doc["feedbackMissing"] = feedbackMissing;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * GET /api/calcurve - Return the active calibration curve (duty vs speed)
 * Samples the live feed-forward mapping so the trace reflects both the
 * built-in presets and a custom-built calibration.
 */
void handleGetCalCurve(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["pwmMax"]          = PWM_DUTY_MAX;
  doc["maxSpeed"]        = maxSpeed;
  doc["calibrationText"] = getCalibrationText(motorPerformanceVal);
  doc["custom"]          = (motorPerformanceVal == CUSTOM_CAL_ID && customCalValid);

  JsonArray speeds = doc["speed"].to<JsonArray>();
  JsonArray duties = doc["duty"].to<JsonArray>();

  uint16_t top = (maxSpeed < 10) ? 200 : maxSpeed;
  uint16_t step = top / 80;
  if (step < 1) step = 1;
  for (uint16_t s = 0; s <= top; s += step) {
    speeds.add(s);
    duties.add((uint32_t)speedToPwmDuty(s));
  }
  if ((top % step) != 0) {          // always include the exact max-speed point
    speeds.add(top);
    duties.add((uint32_t)speedToPwmDuty(top));
  }

  // Anchor points for the ACTIVE cal: the real captured points for a custom cal,
  // or reference marks sampled on the curve for a preset. Both lie on the curve.
  JsonArray anchorSpeeds = doc["anchorSpeed"].to<JsonArray>();
  JsonArray anchorDuties = doc["anchorDuty"].to<JsonArray>();
  if (motorPerformanceVal == CUSTOM_CAL_ID && customCalValid) {
    for (uint8_t i = 0; i < customCalCount; i++) {
      anchorSpeeds.add(customCalPoints[i].speed);
      anchorDuties.add(customCalPoints[i].duty);
    }
  } else {
    for (uint16_t s = 20; s <= top; s += 20) {
      uint32_t d = (uint32_t)speedToPwmDuty(s);
      if (d == 0) continue;
      anchorSpeeds.add(s);
      anchorDuties.add(d);
    }
  }

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
    uint16_t v = value.as<uint16_t>();
    if (v < 10) v = 10;  // never let the feedback freq-scale divisor collapse to ~0
    maxSpeed = v;
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
    // Lock out the vehicle hall input: drop any reading so a stray/previous
    // incoming signal can't linger on the display or drive the motor while the
    // test is active (the ISR already ignores new pulses in test/cal mode).
    dutyCycleIncoming = 0;
    requestedSpeed = 0;
    resetMedianFilter();
    if (!testSpeedo) {
      tempSpeed = 0;
      dutyCycle = 0;
      appliedDutyCycle = 0;
      setMotorDuty(0);
    }
  } else if (strcmp(key, "tempSpeed") == 0) {
    tempSpeed = value.as<uint16_t>();
    if (testSpeedo) {
      testSpeed();  // apply immediately, don't wait for next task cycle
    }
  } else if (strcmp(key, "testCal") == 0) {
    testCal = value.as<bool>();
    // Same hall lockout for calibration mode.
    dutyCycleIncoming = 0;
    requestedSpeed = 0;
    resetMedianFilter();
    if (!testCal) {
      dutyCycle = 0;
      appliedDutyCycle = 0;
      setMotorDuty(0);
    }
  } else if (strcmp(key, "averageFilter") == 0) {
    uint8_t newVal = value.as<uint8_t>();
    if (newVal < 1) newVal = 1;
    if (newVal > 10) newVal = 10;
    averageFilter = newVal;
    resetMedianFilter();
  } else if (strcmp(key, "reverseDirection") == 0) {
    reverseDirection = value.as<bool>();
    applyDirection();
  } else if (strcmp(key, "feedbackEnable") == 0) {
    feedbackEnable = value.as<bool>();
    resetPid();
  } else if (strcmp(key, "pidKp") == 0) {
    pidKp = value.as<float>();
  } else if (strcmp(key, "pidKi") == 0) {
    pidKi = value.as<float>();
  } else if (strcmp(key, "pidKd") == 0) {
    pidKd = value.as<float>();
  } else if (strcmp(key, "feedbackDeadband") == 0) {
    feedbackDeadband = value.as<float>();
  } else if (strcmp(key, "feedbackMinSpeed") == 0) {
    feedbackMinSpeed = value.as<uint16_t>();
  } else {
    request->send(400, "application/json", "{\"error\":\"Unknown setting\"}");
    return;
  }

  if (strcmp(key, "curveOffset0") == 0 ||
      strcmp(key, "curveOffset1") == 0 || strcmp(key, "curveOffset2") == 0 ||
      strcmp(key, "curveOffset3") == 0 || strcmp(key, "curveOffset4") == 0) {
    normaliseSpeedOffsetCurve();
  }

  DEBUG_WEB("set %s = %s", key, value.as<String>().c_str());
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
      tempDutyCycle = PWM_DUTY_MAX;
    } else {
      tempDutyCycle = tempDutyCycle - 1;
    }
  } else if (strcmp(action, "calNext") == 0) {
    if (tempDutyCycle >= PWM_DUTY_MAX) {
      tempDutyCycle = 0;
    } else {
      tempDutyCycle = tempDutyCycle + 1;
    }
  } else {
    request->send(400, "application/json", "{\"error\":\"Unknown action\"}");
    return;
  }

  DEBUG_WEB("action executed: %s", action);
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// ===========================================================================
// SpeedPulserV2 custom calibration builder
// ===========================================================================

// Serialise the current calibration-builder state into a JsonDocument. Shared
// by GET /api/cal and the POST responses so the UI always gets a fresh view.
static void fillCalState(JsonDocument &doc) {
  doc["name"]     = customCalName;
  doc["unit"]     = customCalUnitMph ? "mph" : "kmh";
  doc["convertToMPH"] = convertToMPH;   // lets the UI mirror an auto-enabled MPH cluster
  doc["count"]    = customCalCount;
  doc["valid"]    = customCalValid;
  doc["selected"] = (motorPerformanceVal == CUSTOM_CAL_ID);
  doc["duty"]     = tempDutyCycle;
  doc["pwmMax"]   = PWM_DUTY_MAX;
  doc["maxSpeed"] = maxSpeed;
  doc["feedbackMaxFreq"] = feedbackMaxFreq;   // may have been auto-set on capture

  JsonArray pts = doc["points"].to<JsonArray>();
  for (uint8_t i = 0; i < customCalCount; i++) {
    JsonObject p = pts.add<JsonObject>();
    p["speed"] = customCalPoints[i].speed;
    p["duty"]  = customCalPoints[i].duty;
  }
}

/**
 * GET /api/cal - Return the custom calibration builder state
 */
void handleGetCal(AsyncWebServerRequest *request) {
  JsonDocument doc;
  fillCalState(doc);
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

/**
 * POST /api/cal - Custom calibration builder operations
 * Expected JSON: { "op": "jog|setDuty|addPoint|deletePoint|clearPoints|
 *                        setName|apply|save|export|import", ... }
 */
void handlePostCal(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index + len != total) {
    return;  // Wait for complete payload
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char *op = doc["op"];
  if (!op) {
    request->send(400, "application/json", "{\"error\":\"Missing op\"}");
    return;
  }

  if (strcmp(op, "jog") == 0) {
    int32_t delta = doc["delta"] | 0;
    int32_t range = (int32_t)PWM_DUTY_MAX + 1;
    // Roll over: past max wraps to 0, below 0 wraps to max (easier initial cal).
    int32_t next = ((int32_t)tempDutyCycle + delta) % range;
    if (next < 0) next += range;
    tempDutyCycle = (uint16_t)next;

  } else if (strcmp(op, "setDuty") == 0) {
    int32_t duty = doc["duty"] | 0;
    if (duty < 0) duty = 0;
    if (duty > (int32_t)PWM_DUTY_MAX) duty = PWM_DUTY_MAX;
    tempDutyCycle = (uint16_t)duty;

  } else if (strcmp(op, "addPoint") == 0) {
    uint16_t speed = doc["speed"] | 0;
    // Cal building is open-loop: capture the hand-jogged duty for this speed.
    uint16_t duty  = doc["duty"].isNull() ? tempDutyCycle : doc["duty"].as<uint16_t>();
    if (!calAddPoint(speed, duty)) {
      request->send(409, "application/json", "{\"error\":\"Point list full\"}");
      return;
    }

  } else if (strcmp(op, "deletePoint") == 0) {
    uint8_t idx = doc["index"] | 0;
    calDeletePoint(idx);

  } else if (strcmp(op, "clearPoints") == 0) {
    calClearPoints();

  } else if (strcmp(op, "setName") == 0) {
    const char *name = doc["name"];
    if (name) calSetName(name);
    customCalUnitMph = convertToMPH;  // capture current cluster unit as metadata

  } else if (strcmp(op, "apply") == 0) {
    buildCustomCalTable();
    if (customCalValid) {
      motorPerformanceVal = CUSTOM_CAL_ID;
      updateMotorPerformance = true;
    }

  } else if (strcmp(op, "save") == 0) {
    customCalUnitMph = convertToMPH;
    calSaveToNvs();
    buildCustomCalTable();
    if (customCalValid) {
      motorPerformanceVal = CUSTOM_CAL_ID;  // persisted by the periodic writeEEP
      updateMotorPerformance = true;
    }

  } else if (strcmp(op, "export") == 0) {
    JsonDocument out;
    String json, carray;
    calExportJson(json);
    calExportCArray(carray);
    out["json"]   = json;
    out["carray"] = carray;
    String response;
    serializeJson(out, response);
    request->send(200, "application/json", response);
    return;

  } else if (strcmp(op, "import") == 0) {
    const char *json = doc["json"];
    if (!json || !calImportJson(json)) {
      request->send(400, "application/json", "{\"error\":\"Import failed\"}");
      return;
    }

  } else {
    request->send(400, "application/json", "{\"error\":\"Unknown op\"}");
    return;
  }

  // A custom cal captured in MPH implies the cluster reads MPH; auto-enable the
  // runtime "Cluster in MPH" conversion whenever such a cal is the active one.
  if (motorPerformanceVal == CUSTOM_CAL_ID && customCalValid && customCalUnitMph) {
    convertToMPH = true;
  }

  DEBUG_WEB("cal op: %s (count=%u duty=%u)", op, (unsigned)customCalCount, (unsigned)tempDutyCycle);

  JsonDocument state;
  fillCalState(state);
  String response;
  serializeJson(state, response);
  request->send(200, "application/json", response);
}

