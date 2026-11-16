#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"

// ---------------------------------------------------------------------------
// Settings in NVS (ESP32 Preferences).
//
// Every setting lives in ONE namespace, "hasNeedleSweep". (Up to V5.05 the code
// called begin() once per setting name, but only the first call opens a
// namespace, so that is where everything was saved. Renaming it would lose every
// unit's settings.) Key names are kept as they were for the same reason, even
// where the variable has a clearer name now.
//
// Speed Test is never saved: a unit always starts in normal running.
// ---------------------------------------------------------------------------

static const char *SETTINGS_NAMESPACE = "hasNeedleSweep";

// The drive calibration table. On load the count comes from the table's own size;
// the stored count only says "cleared" (0). So a non-zero count is written BEFORE
// the table (V5.09): a power cut between the two writes then leaves the old table
// (or none), never "0 marks" over a freshly written table. V5.05 and before needed
// the two to agree, so a cut between them dropped the whole table.
//
// A loaded table is checked (V5.09): marks in rising speed order, speeds 1-400,
// drives above 0 and no higher than the rail ceiling. Anything else is a corrupt
// table: it would set a wrong top-speed cap or a wild drive, so it is dropped.
static bool driveCalTableSane(const DriveCalPoint *pts, uint8_t n)
{
  for (uint8_t i = 0; i < n; i++)
  {
    const DriveCalPoint &p = pts[i];
    if (p.speed < 1 || p.speed > 400 || !(p.drive > 0.0f) || p.drive > RAIL_V_CEIL || !(p.hz >= 0.0f))
      return false;
    if (i > 0 && p.speed <= pts[i - 1].speed)
      return false;
  }
  return true;
}
static void loadDriveCalTable()
{
  driveCalCount = 0;
  const size_t blobBytes = settingsStore.getBytesLength("dcPts");
  uint8_t n = (blobBytes % sizeof(DriveCalPoint) == 0) ? (uint8_t)(blobBytes / sizeof(DriveCalPoint)) : 0;
  if (n > DRIVE_CAL_MAX)
    n = 0;
  // A table cleared by V5.05 or older left its old blob behind with dcN = 0.
  if (settingsStore.isKey("dcN") && settingsStore.getUChar("dcN", 0) == 0)
    n = 0;
  if (n > 0)
  {
    DriveCalPoint loaded[DRIVE_CAL_MAX];
    settingsStore.getBytes("dcPts", loaded, sizeof(DriveCalPoint) * n);
    if (!driveCalTableSane(loaded, n))
    {
      DEBUG_EEP("drive cal table corrupt (%u marks): dropped", n);
      return;
    }
    memcpy(driveCal, loaded, sizeof(DriveCalPoint) * n);
    driveCalCount = n;
    // V5.05 keeps each mark's source (eye / auto) in what used to be struct
    // padding. Tables saved before that ("dcSrcV" missing) hold whatever the
    // padding was, and every mark in them was set by eye.
    if (!settingsStore.isKey("dcSrcV"))
    {
      for (uint8_t i = 0; i < n; i++)
      {
        driveCal[i].src = DCAL_SRC_EYE;
        driveCal[i].rsv = 0;
      }
    }
  }
  else if (!settingsStore.isKey("dcN"))
  {
    // V5.00 stored one top-speed point; carry it over as the first mark.
    const uint16_t topSpd = settingsStore.getUShort("calTopSpd", 0);
    const float topHz = settingsStore.getFloat("calTopHz", 0.0f);
    const float topV = settingsStore.getFloat("calTopV", 0.0f);
    const float topPwm = settingsStore.getFloat("calTopPwm", 0.0f);
    if (topSpd > 0 && topV > 0.0f && topPwm > 0.0f)
    {
      driveCal[0] = {topSpd, DCAL_SRC_EYE, 0, topHz, topV * topPwm, topV, topPwm};
      driveCalCount = 1;
      DEBUG_EEP("migrated V5.00 top point %u = %.1f Hz @ %.2f V x %.0f%%", topSpd, topHz, topV, topPwm * 100.0f);
    }
  }
}

static void saveDriveCalTable()
{
  DriveCalPoint pts[DRIVE_CAL_MAX];
  const uint8_t n = driveCalSnapshot(pts); // a consistent copy: another task may be changing it
  if (n > 0)
  {
    settingsStore.putUChar("dcN", n); // first: see the note above loadDriveCalTable()
    settingsStore.putBytes("dcPts", pts, sizeof(DriveCalPoint) * n);
  }
  else
  {
    settingsStore.putUChar("dcN", 0);
    if (settingsStore.isKey("dcPts"))
      settingsStore.remove("dcPts");
  }
  settingsStore.putUChar("dcSrcV", 1); // marks carry their source (V5.05)
}

// Keys older versions wrote that nothing reads any more (V5.09). Each stays until
// the setting that replaced it has been saved once, so a unit still migrates.
static void removeRetiredKeys()
{
  static const char *const RETIRED[] = {"averageFilter", "fbCaptured", "fbMaxFreq", "pidKp", "pidKi", "pidKd",
                                        "tempSpeed", "testSpeedo", "vcEnable"};
  for (const char *key : RETIRED)
    if (settingsStore.isKey(key))
      settingsStore.remove(key);
  struct Replaced { const char *oldKey; const char *newKey; };
  static const Replaced REPLACED[] = {{"maxFreqHall", "pulsesPerKm"}, {"speedOffset", "spdOffsetKph"},
                                      {"offsetPositive", "spdOffsetKph"}, {"calTopSpd", "dcN"},
                                      {"calTopHz", "dcN"}, {"calTopV", "dcN"}, {"calTopPwm", "dcN"}};
  for (const Replaced &r : REPLACED)
    if (settingsStore.isKey(r.newKey) && settingsStore.isKey(r.oldKey))
      settingsStore.remove(r.oldKey);
}

void readEEP()
{
  settingsStore.begin(SETTINGS_NAMESPACE, false);

  needleSweepOnBoot = settingsStore.getBool("hasNeedleSweep", needleSweepOnBoot);
  needleSweepSpeed = constrain(settingsStore.getUChar("sweepSpeed", needleSweepSpeed), 1, 50);
  presetId = settingsStore.getUChar("motorPerfVal", presetId);
  maxSpeed = constrain(settingsStore.getUShort("maxSpeed", maxSpeed), 20, 400);
  clusterInMph = settingsStore.getBool("convertToMPH", clusterInMph);
  reverseDirection = settingsStore.getBool("revDir", false); // V5.01 key (see applyDirection)

  // Speed sensor (V5.06): one "pulses per km" setting. Units from before carry
  // their scale over from Max Hall Frequency at Max Speed.
  if (settingsStore.isKey("pulsesPerKm"))
  {
    sensorPulsesPerKm = settingsStore.getFloat("pulsesPerKm", SENSOR_PULSES_PER_KM_DEFAULT);
  }
  else
  {
    const uint16_t oldMaxHallHz = settingsStore.getUShort("maxFreqHall", 0);
    if (oldMaxHallHz > 0 && maxSpeed > 0)
      sensorPulsesPerKm = 3600.0f * (float)oldMaxHallHz / (float)maxSpeed;
  }
  sensorPulsesPerKm = constrain(sensorPulsesPerKm, SENSOR_PULSES_PER_KM_MIN, SENSOR_PULSES_PER_KM_MAX);
  inputSmoothingMs = constrain(settingsStore.getUShort("inSmoothMs", inputSmoothingMs),
                               INPUT_SMOOTHING_MS_MIN, INPUT_SMOOTHING_MS_MAX);

  // Global offset (V5.06): one signed value. Before, a size + a "positive" tick.
  if (settingsStore.isKey("spdOffsetKph"))
  {
    globalSpeedOffsetKph = settingsStore.getShort("spdOffsetKph", 0);
  }
  else
  {
    const int16_t size = (int16_t)settingsStore.getUShort("speedOffset", 0);
    globalSpeedOffsetKph = settingsStore.getBool("offsetPositive", true) ? size : -size;
  }
  globalSpeedOffsetKph = constrain(globalSpeedOffsetKph, -50, 50);
  useSpeedOffsetCurve = settingsStore.getBool("useCurve", useSpeedOffsetCurve);
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++)
  {
    char key[8];
    snprintf(key, sizeof key, "curveO%u", (unsigned)i);
    speedOffsetCurveOffsets[i] = constrain(settingsStore.getShort(key, 0), -20, 20);
  }

  // Voltage board: feedback (defaults from the V5.04 bench tuning) + drive settings.
  feedbackEnable = settingsStore.getBool("fbEnable", feedbackEnable);
  feedbackDeadband = constrain(settingsStore.getFloat("fbDeadband", feedbackDeadband), 0.0f, 5.0f);
  feedbackMinSpeed = constrain(settingsStore.getUShort("fbMinSpd", feedbackMinSpeed), 0, 100);
  pwmNominal = constrain(settingsStore.getFloat("vcPwmNom", pwmNominal), 0.2f, 1.0f);
  railMaxV = constrain(settingsStore.getFloat("vcRailMax", railMaxV), RAIL_V_FLOOR, RAIL_V_CEIL);
  trimGain = constrain(settingsStore.getFloat("vcTrimG", trimGain), 0.0f, 5.0f);
  trimLimit = constrain(settingsStore.getFloat("vcTrimL", trimLimit), 0.0f, 0.5f);

  loadDriveCalTable();
  // V5.04 Minimum Rail. A unit updated from V5.01-V5.03 has no key: its marks were
  // captured with the rail on the 3.0 V floor at the low end, so keep that split
  // (the same drive at 4.8 V would run the needle roughly twice as fast) until the
  // marks are recaptured. Uncalibrated units start on the 4.8 V default.
  if (settingsStore.isKey("vcRailMin"))
    railMinV = settingsStore.getFloat("vcRailMin", RAIL_V_MIN_DEFAULT);
  else
    railMinV = (driveCalCount > 0) ? RAIL_V_FLOOR : RAIL_V_MIN_DEFAULT;
  railMinV = constrain(railMinV, RAIL_V_FLOOR, railMaxV);
  driveCalBackupValid = settingsStore.isKey("dcBak");

  // Test Board result and this board's own duty -> volts fit.
  boardTestCode = settingsStore.getUChar("btCode", BT_NOT_RUN);
  for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
  {
    char key[8];
    snprintf(key, sizeof key, "btV%u", (unsigned)i);
    boardTestMeasV[i] = settingsStore.getFloat(key, 0.0f);
  }
  railFitV0 = settingsStore.getFloat("btFitV0", RAIL_MODEL_V0);
  railFitSlope = settingsStore.getFloat("btFitK", RAIL_MODEL_SLOPE);
  railFitValid = (boardTestCode == BT_PASS && railFitSlope < -6.0f && railFitSlope > -16.0f &&
                  railFitV0 > 11.0f && railFitV0 < 16.0f); // V5.09: the 0 % duty end checked too
  if (!railFitValid)
  {
    railFitV0 = RAIL_MODEL_V0;
    railFitSlope = RAIL_MODEL_SLOPE;
  }

  DEBUG_EEP("loaded: maxSpeed=%u km/h %.0f pulses/km smoothing=%u ms offset=%d%s mph=%u preset=%u sweep=%u/%u",
            maxSpeed, sensorPulsesPerKm, inputSmoothingMs, globalSpeedOffsetKph,
            useSpeedOffsetCurve ? " (curve)" : "", clusterInMph, presetId, needleSweepOnBoot,
            needleSweepSpeed);
  DEBUG_EEP("voltage: %u marks, rail %.1f-%.1f V, nominal PWM %.0f%%, feedback %s (deadband %.1f Hz, min %u)",
            driveCalCount, railMinV, railMaxV, pwmNominal * 100.0f, feedbackEnable ? "on" : "off",
            feedbackDeadband, feedbackMinSpeed);
  removeRetiredKeys();
}

void writeEEP()
{
  settingsStore.putBool("hasNeedleSweep", needleSweepOnBoot);
  settingsStore.putUChar("sweepSpeed", needleSweepSpeed);
  settingsStore.putUChar("motorPerfVal", presetId);
  settingsStore.putUShort("maxSpeed", maxSpeed);
  settingsStore.putBool("convertToMPH", clusterInMph);
  settingsStore.putBool("revDir", reverseDirection);
  settingsStore.putFloat("pulsesPerKm", sensorPulsesPerKm);
  settingsStore.putUShort("inSmoothMs", inputSmoothingMs);
  settingsStore.putShort("spdOffsetKph", globalSpeedOffsetKph);
  settingsStore.putBool("useCurve", useSpeedOffsetCurve);
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++)
  {
    char key[8];
    snprintf(key, sizeof key, "curveO%u", (unsigned)i);
    settingsStore.putShort(key, speedOffsetCurveOffsets[i]);
  }
  settingsStore.putBool("fbEnable", feedbackEnable);
  settingsStore.putFloat("fbDeadband", feedbackDeadband);
  settingsStore.putUShort("fbMinSpd", feedbackMinSpeed);
  settingsStore.putFloat("vcPwmNom", pwmNominal);
  settingsStore.putFloat("vcRailMax", railMaxV);
  settingsStore.putFloat("vcRailMin", railMinV);
  settingsStore.putFloat("vcTrimG", trimGain);
  settingsStore.putFloat("vcTrimL", trimLimit);
  saveDriveCalTable();
  if (!boardTestActive) // don't save a half-finished test
  {
    settingsStore.putUChar("btCode", boardTestCode);
    for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
    {
      char key[8];
      snprintf(key, sizeof key, "btV%u", (unsigned)i);
      settingsStore.putFloat(key, boardTestMeasV[i]);
    }
    settingsStore.putFloat("btFitV0", railFitV0);
    settingsStore.putFloat("btFitK", railFitSlope);
  }
}
