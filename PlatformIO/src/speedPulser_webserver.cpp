#include "speedPulser_defs.h"
#include "speedPulser_webserver.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include "speedPulser_calBuilder.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"
#include "ota_manager.h"
#include "wifi_manager.h"
#include "speedPulser_resetLog.h"

// ---------------------------------------------------------------------------
// The JSON API behind the web UI.
//
//   GET  /api/settings     every setting, once at page load
//   GET  /api/status       live values, polled while the page is open
//   GET  /api/calibrations preset list (legacy board)
//   GET  /api/calcurve     the legacy speed -> duty curve, for the chart
//   GET  /api/boardtest    Test Board result (voltage board)
//   GET  /api/resetlog     why the unit started + recent starts
//   GET  /api/cal          Calibration Builder state (legacy board)
//   POST /api/control      {key, value}: change one setting; replies with the value kept
//   POST /api/action       {action, ...}: buttons
//   POST /api/cal          {op, ...}: Calibration Builder / export / import
//
// POSTs must be application/json: a page on another site can only send that
// after a preflight this server never answers, so it can't drive the motor.
// Every value is range-checked here: nothing from a request reaches the motor
// unchecked (a cached old page or a hand-made request can't over-drive it).
// ---------------------------------------------------------------------------

static void sendJson(AsyncWebServerRequest *request, int code, JsonDocument &doc)
{
  String body;
  serializeJson(doc, body);
  request->send(code, "application/json", body);
}

static void sendError(AsyncWebServerRequest *request, int code, const char *message)
{
  JsonDocument doc;
  doc["error"] = message;
  sendJson(request, code, doc);
}

// Collect a POST body that may arrive in several TCP chunks. Returns the whole
// body once the last chunk is in, nullptr until then (or after sending an error).
// The buffer rides on _tempObject, which the server frees with the request.
static uint8_t *collectJsonBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                                size_t index, size_t total, size_t maxBytes)
{
  if (!request->contentType().startsWith("application/json"))
  {
    if (index == 0)
      sendError(request, 415, "JSON only");
    return nullptr;
  }
  if (total > maxBytes)
  {
    if (index == 0)
      sendError(request, 413, "Request too large");
    return nullptr;
  }
  if (index == 0)
    request->_tempObject = malloc(total);
  uint8_t *body = (uint8_t *)request->_tempObject;
  if (!body)
  {
    if (index + len == total)
      sendError(request, 500, "Out of memory");
    return nullptr;
  }
  memcpy(body + index, data, len);
  return (index + len == total) ? body : nullptr;
}

// The body handler answers once the whole body is in. With no body at all it is
// never called, so the request itself answers (V5.09: it used to hang to a timeout).
static void attachPost(const char *path, ArBodyHandlerFunction handler)
{
  server.on(path, HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->contentLength() == 0)
      sendError(request, 400, "Empty request");
  }, nullptr, handler);
}

void setupWebServer()
{
  // Shared OTA + Home WiFi routes FIRST: ota_manager's first route carries the
  // filter that notes web activity for every request (otaWebClientActive()),
  // and /api/wifi/sta must precede any /api/wifi... route of our own.
  otaManagerAttach(server);
  wifiManagerAttachSta(server);

  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/calibrations", HTTP_GET, handleGetCalibrations);
  server.on("/api/calcurve", HTTP_GET, handleGetCalCurve);
  server.on("/api/boardtest", HTTP_GET, handleGetBoardTest);
  server.on("/api/resetlog", HTTP_GET, handleGetResetLog);
  server.on("/api/cal", HTTP_GET, handleGetCal);
  attachPost("/api/control", handlePostControl);
  attachPost("/api/action", handlePostAction);
  attachPost("/api/cal", handlePostCal);

  // "/" (the UI, or the recovery page when the filesystem holds no usable UI)
  // + static files with no-cache revalidation.
  wifiManagerAttachStatic(server);
  server.begin();
  DEBUG_WEB("web server started");
}

// ===== Shared pieces =====

// The drive calibration marks (voltage board).
static void addDriveCal(JsonDocument &doc)
{
  JsonArray pts = doc["driveCal"].to<JsonArray>();
  for (uint8_t i = 0; i < driveCalCount; i++)
  {
    JsonObject p = pts.add<JsonObject>();
    p["speed"] = driveCal[i].speed;
    p["hz"] = driveCal[i].hz;
    p["drive"] = driveCal[i].drive;
    p["rail"] = driveCal[i].rail;
    p["pwm"] = driveCal[i].pwm;
    p["src"] = driveCal[i].src == DCAL_SRC_AUTO ? "auto" : "eye";
  }
  doc["driveCalHz"] = driveCalHzValid();
  doc["calBackup"] = driveCalBackupValid; // Restore Previous available
}

// What is driving the motor, for the "Cal:" banner.
static String activeCalText()
{
  if (!boardHasVoltageControl)
    return getCalibrationText(presetId);
  if (driveCalCount == 0)
    return "Not calibrated";
  return String(driveCalCount) + (driveCalCount == 1 ? " mark, " : " marks, ") +
         String(driveCal[0].speed) + "-" + String(driveCalTopSpeed());
}

// ===== GET /api/settings =====
void handleGetSettings(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["fwVersion"] = VERSION;
  doc["isVoltageBoard"] = boardHasVoltageControl;
  doc["calibrationText"] = activeCalText();

  doc["clusterInMph"] = clusterInMph;
  doc["maxSpeed"] = maxSpeed;
  doc["sensorPulsesPerKm"] = sensorPulsesPerKm;
  doc["inputSmoothingMs"] = inputSmoothingMs;
  doc["reverseDirection"] = reverseDirection;
  doc["globalSpeedOffsetKph"] = globalSpeedOffsetKph;
  doc["useSpeedOffsetCurve"] = useSpeedOffsetCurve;
  JsonArray curve = doc["speedOffsetCurveOffsets"].to<JsonArray>();
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++)
    curve.add(speedOffsetCurveOffsets[i]);
  doc["needleSweepOnBoot"] = needleSweepOnBoot;
  doc["needleSweepSpeed"] = needleSweepSpeed;
  doc["speedTestOn"] = speedTestOn;
  doc["speedTestKph"] = speedTestKph;
  doc["calibrationModeOn"] = calibrationModeOn;

  if (boardHasVoltageControl)
  {
    doc["feedbackEnable"] = feedbackEnable;
    doc["feedbackDeadband"] = feedbackDeadband;
    doc["feedbackMinSpeed"] = feedbackMinSpeed;
    doc["trimGain"] = trimGain;
    doc["trimLimit"] = trimLimit;
    doc["pwmNominal"] = pwmNominal;
    doc["railMinV"] = railMinV;
    doc["railMaxV"] = railMaxV;
    doc["railFloorV"] = RAIL_V_FLOOR;
    doc["railCeilV"] = RAIL_V_CEIL;
    doc["boardTestCode"] = boardTestCode;
    doc["driveMaxV"] = driveMaxV();
    addDriveCal(doc);
  }
  else
  {
    doc["presetId"] = presetId;
  }
  sendJson(request, 200, doc);
}

// ===== GET /api/status =====
void handleGetStatus(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["inputFrequencyHz"] = inputFrequencyHz;
  doc["inputSpeedKph"] = inputSpeedKph;
  doc["needleTargetSpeed"] = needleTargetSpeed;
  doc["speedOffsetApplied"] = speedOffsetApplied;
  doc["motorDutyApplied"] = motorDutyApplied;
  doc["speedTestOn"] = speedTestOn;
  doc["speedTestKph"] = speedTestKph;
  doc["calibrationModeOn"] = calibrationModeOn;
  doc["calibrationProbeDuty"] = calibrationProbeDuty;
  doc["calibrationText"] = activeCalText();
  if (boardHasVoltageControl)
  {
    doc["measuredSpeed"] = measuredSpeed;
    doc["measuredFreqHz"] = measuredFreqHz;
    doc["feedbackAvailable"] = feedbackAvailable;
    doc["feedbackMissing"] = feedbackMissing;
    doc["driveTrimPct"] = driveTrim * 100.0f;
    // The trim has run out of room (V5.09): the calibration is off at this speed.
    doc["trimAtLimit"] = trimLimit > 0.0f && fabsf(driveTrim) >= trimLimit - 0.005f;
    doc["pwmFrac"] = lastPwmFrac;
    doc["driveV"] = lastDriveV;
    doc["driveMaxV"] = driveMaxV();
    doc["calibrationDriveV"] = calibrationDriveV;
    doc["railCmdV"] = lastRailCmdV;
    doc["motorRailV"] = motorRailV; // trust it once railSenseOk
    doc["railSenseOk"] = railFitValid;
    doc["railLimited"] = railLimited();
    if (railTripped)
      doc["railTrippedV"] = railTripV; // motor supply shut off: reboot to clear
    doc["boardTestRunning"] = (bool)boardTestActive;
    float fbMeanHz, fbSpreadPct, fbDriftHz;
    const bool fbWin = feedbackHzWindow(fbMeanHz, fbSpreadPct, fbDriftHz);
    doc["fbHzSpread"] = fbWin ? fbSpreadPct : -1.0f; // -1 = not turning
    doc["fbHzDrift"] = fbWin ? fbDriftHz : 0.0f;
    doc["fbStable"] = fbWin && feedbackHzSteady(fbMeanHz, fbSpreadPct, fbDriftHz);
    JsonObject af = doc["autoFill"].to<JsonObject>();
    af["state"] = autoFillState; // 0 idle, 1 running, 2 done, 3 failed
    af["index"] = autoFillIndex;
    af["total"] = autoFillTotal;
    af["speed"] = autoFillSpeed;
    af["hz"] = autoFillTargetHz;
    af["msg"] = autoFillMsg;
    doc["calBackup"] = driveCalBackupValid;
  }
  sendJson(request, 200, doc);
}

// ===== GET /api/calibrations =====
void handleGetCalibrations(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  JsonArray calibrations = doc["calibrations"].to<JsonArray>();
  const uint8_t count = getCalibrationCount();
  for (uint8_t i = 1; i <= count; i++)
  {
    JsonObject item = calibrations.add<JsonObject>();
    item["id"] = i;
    item["name"] = getCalibrationText(i);
  }
  if (customCalCount >= 2) // the Calibration Builder's own calibration, once it has points
  {
    JsonObject item = calibrations.add<JsonObject>();
    item["id"] = CUSTOM_CAL_ID;
    item["name"] = String("★ Custom: ") + customCalName;
  }
  doc["selected"] = presetId;
  sendJson(request, 200, doc);
}

// ===== GET /api/calcurve (legacy board) =====
// The active preset's speed -> duty curve, sampled, for the Dashboard chart.
void handleGetCalCurve(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["calibrationText"] = getCalibrationText(presetId);
  doc["custom"] = (presetId == CUSTOM_CAL_ID && customCalValid);
  JsonArray speeds = doc["speed"].to<JsonArray>();
  JsonArray duties = doc["duty"].to<JsonArray>();
  const uint16_t top = maxSpeed;
  uint16_t step = top / 80;
  if (step < 1)
    step = 1;
  for (uint16_t s = 0; s <= top; s += step)
  {
    speeds.add(s);
    duties.add((uint32_t)speedToPwmDuty(s));
  }
  if ((top % step) != 0)
  {
    speeds.add(top);
    duties.add((uint32_t)speedToPwmDuty(top));
  }
  // Anchor points: the captured points of a custom cal, else marks on the curve.
  JsonArray anchorSpeeds = doc["anchorSpeed"].to<JsonArray>();
  JsonArray anchorDuties = doc["anchorDuty"].to<JsonArray>();
  if (presetId == CUSTOM_CAL_ID && customCalValid)
  {
    for (uint8_t i = 0; i < customCalCount; i++)
    {
      anchorSpeeds.add(customCalPoints[i].speed);
      anchorDuties.add(customCalPoints[i].duty);
    }
  }
  else
  {
    for (uint16_t s = 20; s <= top; s += 20)
    {
      const uint32_t d = speedToPwmDuty(s);
      if (d == 0)
        continue;
      anchorSpeeds.add(s);
      anchorDuties.add(d);
    }
  }
  sendJson(request, 200, doc);
}

// ===== POST /api/control =====
// One setting per request. Replies {"status":"ok","value":<what was kept>} so the
// UI can show the value the unit actually uses (clamped, or refused).
void handlePostControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  uint8_t *body = collectJsonBody(request, data, len, index, total, 512);
  if (!body)
    return;
  JsonDocument doc;
  if (deserializeJson(doc, body, total))
    return sendError(request, 400, "Invalid JSON");
  const char *key = doc["key"];
  JsonVariant value = doc["value"];
  if (!key)
    return sendError(request, 400, "Missing key");

  JsonDocument reply;
  reply["status"] = "ok";
  auto isKey = [&](const char *name) { return strcmp(key, name) == 0; };
  auto clampF = [&](float lo, float hi) { return constrain(value.as<float>(), lo, hi); };
  auto clampI = [&](long lo, long hi) { return constrain(value.as<long>(), lo, hi); };
  const bool voltageKey = isKey("feedbackEnable") || isKey("feedbackDeadband") || isKey("feedbackMinSpeed") ||
                          isKey("trimGain") || isKey("trimLimit") || isKey("pwmNominal") ||
                          isKey("railMinV") || isKey("railMaxV") || isKey("calibrationDriveV");
  if (voltageKey && !boardHasVoltageControl)
    return sendError(request, 409, "Not on this board (no voltage control)");

  if (isKey("needleSweepOnBoot"))
  {
    needleSweepOnBoot = value.as<bool>();
    reply["value"] = needleSweepOnBoot;
  }
  else if (isKey("needleSweepSpeed"))
  {
    needleSweepSpeed = (uint8_t)clampI(1, 50);
    reply["value"] = needleSweepSpeed;
  }
  else if (isKey("presetId"))
  {
    const long id = value.as<long>();
    const bool valid = (id >= 1 && id <= getCalibrationCount()) || (id == CUSTOM_CAL_ID && customCalCount >= 2);
    if (!valid)
      return sendError(request, 400, "No such calibration");
    presetId = (uint8_t)id;
    if (presetId == CUSTOM_CAL_ID)
      calSaveToNvs(); // the 2 s task saves presetId: the points must be saved too
    updateMotorArray();
    reply["value"] = presetId;
  }
  else if (isKey("maxSpeed"))
  {
    maxSpeed = (uint16_t)clampI(20, 400);
    updateInputGlitchFilter();
    reply["value"] = maxSpeed;
  }
  else if (isKey("sensorPulsesPerKm"))
  {
    sensorPulsesPerKm = clampF(SENSOR_PULSES_PER_KM_MIN, SENSOR_PULSES_PER_KM_MAX);
    updateInputGlitchFilter();
    reply["value"] = sensorPulsesPerKm;
  }
  else if (isKey("inputSmoothingMs"))
  {
    inputSmoothingMs = (uint16_t)clampI(INPUT_SMOOTHING_MS_MIN, INPUT_SMOOTHING_MS_MAX);
    reply["value"] = inputSmoothingMs;
  }
  else if (isKey("clusterInMph"))
  {
    // A custom PWM cal's speeds are dial readings in the unit it was captured in.
    const bool mph = value.as<bool>();
    if (!boardHasVoltageControl && presetId == CUSTOM_CAL_ID && customCalValid && mph != customCalUnitMph)
      return sendError(request, 409, customCalUnitMph
                                         ? "The custom calibration was captured in mph: pick a preset or capture it again in km/h"
                                         : "The custom calibration was captured in km/h: pick a preset or capture it again in mph");
    clusterInMph = mph;
    reply["value"] = clusterInMph;
  }
  else if (isKey("globalSpeedOffsetKph"))
  {
    globalSpeedOffsetKph = (int16_t)clampI(-50, 50);
    reply["value"] = globalSpeedOffsetKph;
  }
  else if (isKey("useSpeedOffsetCurve"))
  {
    useSpeedOffsetCurve = value.as<bool>();
    reply["value"] = useSpeedOffsetCurve;
  }
  else if (strncmp(key, "curveOffset", 11) == 0 && key[11] >= '0' && key[11] < '0' + SPEED_OFFSET_CURVE_POINTS && !key[12])
  {
    const uint8_t point = key[11] - '0';
    speedOffsetCurveOffsets[point] = (int16_t)clampI(-20, 20);
    reply["value"] = speedOffsetCurveOffsets[point];
  }
  else if (isKey("speedTestOn"))
  {
    if (boardTestActive)
      return sendError(request, 409, "Board test running");
    speedTestOn = value.as<bool>();
    if (speedTestOn && calibrationModeOn)
    {
      // One test mode at a time: Speed Test switches Calibration Mode off.
      autoFillStop("Stopped: Speed Test was switched on");
      calibrationModeOn = false;
    }
    if (!speedTestOn)
      speedTestKph = 0;
    reply["value"] = speedTestOn;
  }
  else if (isKey("speedTestKph"))
  {
    speedTestKph = (uint16_t)clampI(0, maxSpeed); // never past what the sensor could show
    reply["value"] = speedTestKph;
  }
  else if (isKey("calibrationModeOn"))
  {
    if (boardTestActive)
      return sendError(request, 409, "Board test running");
    const bool wasOn = calibrationModeOn;
    calibrationModeOn = value.as<bool>();
    if (calibrationModeOn)
    {
      speedTestOn = false; // one test mode at a time
      speedTestKph = 0;
      if (!wasOn)
      {
        // Calibration always starts with the motor stopped: the user raises it.
        calibrationDriveV = 0.0f;
        calibrationProbeDuty = 0;
        resetDriveControl();
      }
    }
    else
    {
      autoFillStop("Stopped: Calibration Mode was turned off");
    }
    reply["value"] = calibrationModeOn;
  }
  else if (isKey("reverseDirection"))
  {
    if (!motorIsStopped())
      return sendError(request, 409, "Stop the motor first (no speed, Speed Test off, drive 0)");
    reverseDirection = value.as<bool>();
    applyDirection();
    reply["value"] = reverseDirection;
  }
  else if (isKey("feedbackEnable"))
  {
    feedbackEnable = value.as<bool>();
    resetDriveControl();
    reply["value"] = feedbackEnable;
  }
  else if (isKey("feedbackDeadband"))
  {
    feedbackDeadband = clampF(0.0f, 5.0f);
    reply["value"] = feedbackDeadband;
  }
  else if (isKey("feedbackMinSpeed"))
  {
    feedbackMinSpeed = (uint16_t)clampI(0, 100);
    reply["value"] = feedbackMinSpeed;
  }
  else if (isKey("trimGain"))
  {
    trimGain = clampF(0.0f, 5.0f);
    reply["value"] = trimGain;
  }
  else if (isKey("trimLimit"))
  {
    trimLimit = clampF(0.0f, 0.5f);
    driveTrim = constrain(driveTrim, -trimLimit, trimLimit);
    reply["value"] = trimLimit;
  }
  else if ((isKey("pwmNominal") || isKey("railMinV") || isKey("railMaxV")) && (!calibrationModeOn || autoFillRunning()))
  {
    // They change how every mark's drive is split into rail x PWM, so the needle
    // moves: only in Calibration Mode (V5.08), and not while Auto Fill measures.
    return sendError(request, 409, autoFillRunning() ? "Auto Fill is running" : "Tick Calibration Mode first (Setup, step 3)");
  }
  else if (isKey("pwmNominal"))
  {
    pwmNominal = clampF(0.2f, 1.0f);
    reply["value"] = pwmNominal;
  }
  else if (isKey("railMinV"))
  {
    railMinV = clampF(RAIL_V_FLOOR, railMaxV);
    resetDriveControl();
    reply["value"] = railMinV;
  }
  else if (isKey("railMaxV"))
  {
    railMaxV = clampF(RAIL_V_FLOOR, RAIL_V_CEIL);
    if (railMinV > railMaxV)
      railMinV = railMaxV; // Minimum Rail can never sit above Maximum Rail
    reply["value"] = railMaxV;
    reply["railMinV"] = railMinV;
  }
  else if (isKey("calibrationDriveV"))
  {
    // Calibration drive (V) from the UI's -/+ buttons; applied on the next 100 ms
    // tick. Lowering is unrestricted, but one request may raise it by at most
    // CAL_DRIVE_MAX_STEP_V (the largest button step), so a cached old page or a
    // second browser can't jump the motor voltage and over-drive the gauge.
    if (!calibrationModeOn)
      return sendError(request, 409, "Enable Calibration Mode first");
    if (autoFillRunning())
      return sendError(request, 409, "Auto Fill is running");
    float want = clampF(0.0f, driveMaxV());
    if (want > calibrationDriveV + CAL_DRIVE_MAX_STEP_V)
      want = calibrationDriveV + CAL_DRIVE_MAX_STEP_V;
    calibrationDriveV = want;
    reply["value"] = calibrationDriveV;
  }
  else
  {
    return sendError(request, 400, "Unknown setting");
  }

  DEBUG_WEB("set %s = %s", key, reply["value"].as<String>().c_str());
  sendJson(request, 200, reply);
}

// ===== POST /api/action =====
void handlePostAction(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  uint8_t *body = collectJsonBody(request, data, len, index, total, 512);
  if (!body)
    return;
  JsonDocument doc;
  if (deserializeJson(doc, body, total))
    return sendError(request, 400, "Invalid JSON");
  const char *action = doc["action"];
  if (!action)
    return sendError(request, 400, "Missing action");
  auto isAction = [&](const char *name) { return strcmp(action, name) == 0; };

  if (isAction("needleSweep"))
  {
    // The sweep holds the control task for a few seconds: only with the car stopped.
    if (!motorIsStopped() || calibrationModeOn || boardTestActive || autoFillRunning())
      return sendError(request, 409, "Only with the car stopped and no test running");
    needleSweepRequested = true;
  }
  else if (isAction("boardTest"))
  {
    if (calibrationModeOn || speedTestOn || needleSweepRequested || !motorIsStopped())
      return sendError(request, 409, "Stop the motor and turn off Speed Test / Calibration Mode first");
    if (boardHasVoltageControl && railMaxV < BOARD_TEST_MIN_TOP_V)
      return sendError(request, 409, "Test Board needs Maximum rail of 6 V or more (Advanced, Motor Voltage)");
    if (!startBoardTest())
      return sendError(request, 409, "Board test already running");
  }
  else if (isAction("clearResetLog"))
  {
    resetLogClear();
  }
  else if (isAction("calAddPoint") || isAction("calDeletePoint") || isAction("calClearPoints") ||
           isAction("calAutoFill") || isAction("calAutoFillStop") || isAction("calRestore") ||
           isAction("calSetSource"))
  {
    // Voltage board drive calibration. Each answers with the updated table.
    if (!boardHasVoltageControl)
      return sendError(request, 409, "No voltage control on this board");
    const char *err = nullptr;
    if (isAction("calAutoFillStop"))
    {
      autoFillStop("Stopped");
    }
    else if (isAction("calAutoFill"))
    {
      if (!autoFillStart(err))
        return sendError(request, 409, err);
    }
    else
    {
      // Changing the table while driving would jump the needle: Calibration Mode only.
      if (!calibrationModeOn)
        return sendError(request, 409, "Enable Calibration Mode first");
      if (autoFillRunning())
        return sendError(request, 409, "Auto Fill is running: stop it first");
      if (isAction("calAddPoint"))
      {
        const long speed = doc["speed"] | 0L; // 65537 must not wrap round to 1
        const uint8_t result = driveCalCapture(speed >= 1 && speed <= 400 ? (uint16_t)speed : 0);
        if (result != DCAL_OK)
          return sendError(request, 409, driveCalErrorText(result));
      }
      else if (isAction("calDeletePoint"))
      {
        driveCalDelete(doc["index"] | 255);
      }
      else if (isAction("calSetSource"))
      {
        // Tap a mark's eye / auto pill: Auto Fill keeps eye marks and replaces auto ones.
        if (!driveCalSetSource(doc["index"] | 255, (doc["auto"] | false) ? DCAL_SRC_AUTO : DCAL_SRC_EYE))
          return sendError(request, 400, "No such mark");
      }
      else if (isAction("calClearPoints"))
      {
        driveCalClear();
      }
      else if (!driveCalRestoreBackup(err))
      {
        return sendError(request, 409, err);
      }
    }
    JsonDocument out;
    out["status"] = "ok";
    addDriveCal(out);
    return sendJson(request, 200, out);
  }
  else
  {
    return sendError(request, 400, "Unknown action");
  }
  DEBUG_WEB("action: %s", action);
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// ===== GET /api/resetlog =====
void handleGetResetLog(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  resetLogToJson(doc);
  sendJson(request, 200, doc);
}

// ===== GET /api/boardtest =====
void handleGetBoardTest(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["running"] = (bool)boardTestActive;
  doc["code"] = boardTestCode;
  doc["text"] = boardTestText(boardTestCode);
  doc["pass"] = (boardTestCode == BT_PASS);
  JsonArray pts = doc["points"].to<JsonArray>();
  for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
  {
    JsonObject p = pts.add<JsonObject>();
    p["target"] = boardTestTargetV[i];
    p["measured"] = boardTestMeasV[i];
  }
  sendJson(request, 200, doc);
}

// ===========================================================================
// Calibration Builder (legacy board) + export / import (both boards)
// ===========================================================================
static void fillCalState(JsonDocument &doc)
{
  doc["name"] = customCalName;
  doc["clusterInMph"] = clusterInMph; // a custom cal captured in mph switches the dial to mph
  doc["count"] = customCalCount;
  doc["selected"] = (presetId == CUSTOM_CAL_ID);
  doc["duty"] = calibrationProbeDuty;
  JsonArray pts = doc["points"].to<JsonArray>();
  for (uint8_t i = 0; i < customCalCount; i++)
  {
    JsonObject p = pts.add<JsonObject>();
    p["speed"] = customCalPoints[i].speed;
    p["duty"] = customCalPoints[i].duty;
  }
}

void handleGetCal(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  fillCalState(doc);
  sendJson(request, 200, doc);
}

void handlePostCal(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  // An imported file can span several TCP chunks.
  uint8_t *body = collectJsonBody(request, data, len, index, total, 16384);
  if (!body)
    return;
  JsonDocument doc;
  if (deserializeJson(doc, body, total))
    return sendError(request, 400, "Invalid JSON");
  const char *op = doc["op"];
  if (!op)
    return sendError(request, 400, "Missing op");
  auto isOp = [&](const char *name) { return strcmp(op, name) == 0; };

  // The raw-duty ops are for the legacy board only: on a voltage board the
  // motor is never driven by a raw duty (it has the drive buttons instead).
  // Jogging only drives the motor in Calibration Mode, so a point captured outside
  // it would store a duty the needle never showed; and with the custom cal selected
  // any point change moves the running needle.
  const bool editOp = isOp("jog") || isOp("addPoint") || isOp("deletePoint") || isOp("clearPoints") ||
                      isOp("apply") || isOp("save");
  // The Calibration Builder is the legacy board's: on a voltage board it would select
  // a PWM table that never drives the motor (V5.09: only jog / addPoint were refused).
  if (editOp && boardHasVoltageControl)
    return sendError(request, 409, "Not on a voltage board: use Drive Calibration");
  if (editOp && !calibrationModeOn)
    return sendError(request, 409, "Tick Calibration Mode first");

  if (isOp("jog"))
  {
    // Legacy board: past max wraps to 0, below 0 wraps to max, so the first
    // calibration step (pot at minimum, duty at maximum) is one press away.
    const int32_t delta = doc["delta"] | 0;
    const int32_t range = (int32_t)PWM_DUTY_MAX + 1;
    int32_t next = ((int32_t)calibrationProbeDuty + delta) % range;
    if (next < 0)
      next += range;
    calibrationProbeDuty = (uint16_t)next;
  }
  else if (isOp("addPoint"))
  {
    const long speedIn = doc["speed"] | 0L; // 65537 must not wrap round to 1
    const uint16_t speed = speedIn >= 1 && speedIn <= 400 ? (uint16_t)speedIn : 0;
    const uint16_t duty = calibrationProbeDuty; // the duty the needle is showing now
    const char *err = nullptr;
    if (!calAddPoint(speed, duty, err))
      return sendError(request, 409, err);
  }
  else if (isOp("deletePoint"))
  {
    if (doc["index"].isNull())
      return sendError(request, 400, "Missing index");
    calDeletePoint(doc["index"].as<uint8_t>());
  }
  else if (isOp("clearPoints"))
  {
    calClearPoints();
  }
  else if (isOp("setName"))
  {
    const char *name = doc["name"];
    if (name)
      calSetName(name);
    customCalUnitMph = clusterInMph; // the dial unit the points were read in
  }
  else if (isOp("apply") || isOp("save"))
  {
    // Both save the points (V5.08): Apply used to select the custom cal without
    // saving them, so the next ignition-on found no points and a dead speedo.
    if (customCalCount < 2)
      return sendError(request, 409, "Capture at least 2 points first");
    customCalUnitMph = clusterInMph;
    calSaveToNvs();
    presetId = CUSTOM_CAL_ID; // saved by the 2 s settings task
    updateMotorArray();
  }
  else if (isOp("export"))
  {
    JsonDocument out;
    String json, carray;
    calExportJson(json);
    calExportCArray(carray);
    out["json"] = json;     // PWM calibration (legacy)
    out["carray"] = carray; // the same as a firmware preset
    if (boardHasVoltageControl)
    {
      String voltage;
      driveCalExportJson(voltage);
      out["voltage"] = voltage; // voltage (drive) calibration
      out["voltageMarks"] = driveCalCount;
    }
    return sendJson(request, 200, out);
  }
  else if (isOp("import"))
  {
    const char *json = doc["json"];
    const char *err = nullptr;
    // One Import button for both kinds: the file's "type" says which it is.
    if (json && strstr(json, DRIVE_CAL_FILE_TYPE))
    {
      if (!boardHasVoltageControl)
        return sendError(request, 409, "This is a voltage calibration: this board has no voltage control");
      if (!calibrationModeOn)
        return sendError(request, 409, "Enable Calibration Mode first (Calibrate card)");
      if (autoFillRunning())
        return sendError(request, 409, "Auto Fill is running: stop it first");
      if (!driveCalImportJson(json, err))
        return sendError(request, 400, err);
      JsonDocument out;
      out["kind"] = "voltage";
      addDriveCal(out);
      return sendJson(request, 200, out);
    }
    if (boardHasVoltageControl)
      return sendError(request, 409, "This is a PWM calibration (legacy boards): a voltage board uses drive marks");
    if (!calImportJson(json, err))
      return sendError(request, 400, err);
  }
  else
  {
    return sendError(request, 400, "Unknown op");
  }

  JsonDocument out;
  fillCalState(out);
  sendJson(request, 200, out);
}
