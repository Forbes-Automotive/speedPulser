#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"

void readEEP() {
  DEBUG_EEP("initialising...");

  // use ESP32's 'Preferences' to remember settings.  Begin by opening the various types.  Use 'false' for read/write.  True just gives read access
  pref.begin("hasNeedleSweep", false);
  pref.begin("testSpeedo", false);
  pref.begin("offsetPositive", false);
  pref.begin("tempSpeed", false);
  pref.begin("maxFreqHall", false);
  pref.begin("maxSpeed", false);
  pref.begin("speedOffset", false);
  pref.begin("convertToMPH", false);
  pref.begin("motorPerfVal", false);
  pref.begin("sweepSpeed", false);
  pref.begin("averageFilter", false);
  pref.begin("useCurve", false);
  pref.begin("curveO0", false);
  pref.begin("curveO1", false);
  pref.begin("curveO2", false);
  pref.begin("curveO3", false);
  pref.begin("curveO4", false);
  pref.begin("revDir", false);
  pref.begin("fbEnable", false);
  pref.begin("pidKp", false);
  pref.begin("pidKi", false);
  pref.begin("pidKd", false);
  pref.begin("fbDeadband", false);
  pref.begin("fbMaxFreq", false);

  // first run comes with EEP valve of 255, so write actual values.  If found/match SW version, read all the values
  if (pref.getUChar("testSpeedo") == 255) {
    DEBUG_EEP("first run detected (raw=%u) — writing default settings", pref.getUChar("testSpeedo"));
    pref.putBool("hasNeedleSweep", hasNeedleSweep);
    pref.putBool("testSpeedo", testSpeedo);
    pref.putBool("offsetPositive", speedOffsetPositive);
    pref.putUShort("tempSpeed", tempSpeed);
    pref.putUShort("maxFreqHall", maxFreqHall);
    pref.putUShort("maxSpeed", maxSpeed);
    pref.putUShort("speedOffset", speedOffset);
    pref.putBool("convertToMPH", convertToMPH);
    pref.putUChar("motorPerfVal", motorPerformanceVal);
    pref.putUChar("sweepSpeed", sweepSpeed);
    pref.putUChar("averageFilter", averageFilter);
    pref.putBool("useCurve", useSpeedOffsetCurve);
    pref.putShort("curveO0", speedOffsetCurveOffsets[0]);
    pref.putShort("curveO1", speedOffsetCurveOffsets[1]);
    pref.putShort("curveO2", speedOffsetCurveOffsets[2]);
    pref.putShort("curveO3", speedOffsetCurveOffsets[3]);
    pref.putShort("curveO4", speedOffsetCurveOffsets[4]);
    pref.putBool("revDir", reverseDirection);
    pref.putBool("fbEnable", feedbackEnable);
    pref.putFloat("pidKp", pidKp);
    pref.putFloat("pidKi", pidKi);
    pref.putFloat("pidKd", pidKd);
    pref.putFloat("fbDeadband", feedbackDeadband);
    pref.putUShort("fbMinSpd", feedbackMinSpeed);
    pref.putUShort("fbMaxFreq", feedbackMaxFreq);
    pref.putBool("fbCaptured", topSpeedCaptured);
    pref.putBool("vcEnable", voltageControlEnable);
    pref.putFloat("vcPwmNom", vcPwmNominal);
  } else {
    hasNeedleSweep = pref.getBool("hasNeedleSweep", false);
    testSpeedo = pref.getBool("testSpeedo", false);
    speedOffsetPositive = pref.getBool("offsetPositive", true);
    tempSpeed = pref.getUShort("tempSpeed", 100);
    maxFreqHall = pref.getUShort("maxFreqHall", 200);
    maxSpeed = pref.getUShort("maxSpeed", 200);
    if (maxSpeed < 10) maxSpeed = 200;  // guard: maxSpeed is the feedback freq-scale divisor, never ~0
    speedOffset = pref.getUShort("speedOffset", 0);
    convertToMPH = pref.getBool("convertToMPH", false);
    motorPerformanceVal = pref.getUChar("motorPerfVal", 0);
    sweepSpeed = pref.getUChar("sweepSpeed", 18);
    averageFilter = pref.getUChar("averageFilter", 6);
    useSpeedOffsetCurve = pref.getBool("useCurve", false);
    speedOffsetCurveOffsets[0] = pref.getShort("curveO0", 0);
    speedOffsetCurveOffsets[1] = pref.getShort("curveO1", 0);
    speedOffsetCurveOffsets[2] = pref.getShort("curveO2", 0);
    speedOffsetCurveOffsets[3] = pref.getShort("curveO3", 0);
    speedOffsetCurveOffsets[4] = pref.getShort("curveO4", 0);
    feedbackEnable = pref.getBool("fbEnable", false);
    pidKp = pref.getFloat("pidKp", 0.15f);
    pidKi = pref.getFloat("pidKi", 1.3f);
    pidKd = pref.getFloat("pidKd", 0.0f);
    feedbackDeadband = pref.getFloat("fbDeadband", 1.5f);
    feedbackMinSpeed = pref.getUShort("fbMinSpd", 40);
    feedbackMaxFreq = pref.getUShort("fbMaxFreq", feedbackMaxFreq);
    // Units updated from older firmware have no flag yet: treat any value other
    // than the 254 Hz placeholder as a real capture so they keep their closed loop.
    topSpeedCaptured = pref.getBool("fbCaptured", feedbackMaxFreq != 254);
    voltageControlEnable = pref.getBool("vcEnable", true);
    vcPwmNominal = pref.getFloat("vcPwmNom", vcPwmNominal);
  }

  // V5 Test Board result and V5.01 drive settings + calibration marks. Read
  // outside the first-run branch: units updated from older firmware have none of
  // these keys yet, so they come up with the defaults / "not calibrated".
  // Direction: V5.01 inverted the pin sense (unticked = correct way) and moved
  // it from "reverseDir" to "revDir", so the old, inverted value is ignored and
  // every unit comes up spinning the correct way.
  reverseDirection = pref.getBool("revDir", false);
  vcRailMaxV = pref.getFloat("vcRailMax", vcRailMaxV);
  vcTrimGain = pref.getFloat("vcTrimG", vcTrimGain);
  vcTrimLimit = pref.getFloat("vcTrimL", vcTrimLimit);
  driveCalCount = 0;
  const uint8_t dcN = pref.getUChar("dcN", 0);
  if (dcN > 0 && dcN <= DRIVE_CAL_MAX &&
      pref.getBytesLength("dcPts") == sizeof(DriveCalPoint) * dcN) {
    pref.getBytes("dcPts", driveCal, sizeof(DriveCalPoint) * dcN);
    driveCalCount = dcN;
  } else if (!pref.isKey("dcN")) {
    // V5.00 stored one top-speed point; carry it over as the first mark.
    const uint16_t topSpd = pref.getUShort("calTopSpd", 0);
    const float topHz = pref.getFloat("calTopHz", 0.0f);
    const float topV = pref.getFloat("calTopV", 0.0f);
    const float topPwm = pref.getFloat("calTopPwm", 0.0f);
    if (topSpd > 0 && topV > 0.0f && topPwm > 0.0f) {
      driveCal[0] = {topSpd, topHz, topV * topPwm, topV, topPwm};
      driveCalCount = 1;
      DEBUG_EEP("migrated V5.00 top point %u = %.1f Hz @ %.2f V x %.0f%%", topSpd, topHz, topV, topPwm * 100.0f);
    }
  }
  if (boardHasVoltageControl) topSpeedCaptured = (driveCalCount > 0);
  boardTestCode = pref.getUChar("btCode", BT_NOT_RUN);
  for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++) {
    char key[8];
    snprintf(key, sizeof key, "btV%u", (unsigned)i);
    boardTestMeasV[i] = pref.getFloat(key, 0.0f);
  }
  railFitV0 = pref.getFloat("btFitV0", RAIL_MODEL_V0);
  railFitSlope = pref.getFloat("btFitK", RAIL_MODEL_SLOPE);
  railFitValid = (boardTestCode == BT_PASS && railFitSlope < -6.0f && railFitSlope > -16.0f);
  if (!railFitValid) {
    railFitV0 = RAIL_MODEL_V0;
    railFitSlope = RAIL_MODEL_SLOPE;
  }
  syncFeedbackMaxFreq(); // Hz-at-maxSpeed follows the captured point

  normaliseSpeedOffsetCurve();
  DEBUG_EEP("loaded: sweep=%u speedo=%u off+=%u tempSpd=%u maxHz=%u maxSpd=%u offset=%u mph=%u calVal=%u sweepRate=%u curve=%u",
            (unsigned)hasNeedleSweep, (unsigned)testSpeedo, (unsigned)speedOffsetPositive,
            (unsigned)tempSpeed, (unsigned)maxFreqHall, (unsigned)maxSpeed, (unsigned)speedOffset,
            (unsigned)convertToMPH, (unsigned)motorPerformanceVal, (unsigned)sweepSpeed,
            (unsigned)useSpeedOffsetCurve);
  DEBUG_EEP("feedback: dir=%s en=%u Kp=%.2f Ki=%.2f Kd=%.2f fbMaxHz=%u",
            reverseDirection ? "REV" : "FWD", (unsigned)feedbackEnable,
            pidKp, pidKi, pidKd, (unsigned)feedbackMaxFreq);
}

void writeEEP() {
  // update EEP only if changes have been made
  pref.putBool("hasNeedleSweep", hasNeedleSweep);
  pref.putBool("testSpeedo", testSpeedo);
  pref.putBool("offsetPositive", speedOffsetPositive);
  pref.putUShort("tempSpeed", tempSpeed);
  pref.putUShort("maxFreqHall", maxFreqHall);
  pref.putUShort("maxSpeed", maxSpeed);
  pref.putUShort("speedOffset", speedOffset);
  pref.putBool("convertToMPH", convertToMPH);
  pref.putUChar("motorPerfVal", motorPerformanceVal);
  pref.putUChar("sweepSpeed", sweepSpeed);
  pref.putUChar("averageFilter", averageFilter);
  pref.putBool("useCurve", useSpeedOffsetCurve);
  pref.putShort("curveO0", speedOffsetCurveOffsets[0]);
  pref.putShort("curveO1", speedOffsetCurveOffsets[1]);
  pref.putShort("curveO2", speedOffsetCurveOffsets[2]);
  pref.putShort("curveO3", speedOffsetCurveOffsets[3]);
  pref.putShort("curveO4", speedOffsetCurveOffsets[4]);
  pref.putBool("revDir", reverseDirection);
  pref.putBool("fbEnable", feedbackEnable);
  pref.putFloat("pidKp", pidKp);
  pref.putFloat("pidKi", pidKi);
  pref.putFloat("pidKd", pidKd);
  pref.putFloat("fbDeadband", feedbackDeadband);
  pref.putUShort("fbMinSpd", feedbackMinSpeed);
  pref.putUShort("fbMaxFreq", feedbackMaxFreq);
  pref.putBool("fbCaptured", topSpeedCaptured);
  pref.putBool("vcEnable", voltageControlEnable);
  pref.putFloat("vcPwmNom", vcPwmNominal);
  pref.putFloat("vcRailMax", vcRailMaxV);
  pref.putFloat("vcTrimG", vcTrimGain);
  pref.putFloat("vcTrimL", vcTrimLimit);
  pref.putUChar("dcN", driveCalCount);
  if (driveCalCount > 0) {
    pref.putBytes("dcPts", driveCal, sizeof(DriveCalPoint) * driveCalCount);
  }
  if (!boardTestActive) { // don't save a half-finished test
    pref.putUChar("btCode", boardTestCode);
    for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++) {
      char key[8];
      snprintf(key, sizeof key, "btV%u", (unsigned)i);
      pref.putFloat(key, boardTestMeasV[i]);
    }
    pref.putFloat("btFitV0", railFitV0);
    pref.putFloat("btFitK", railFitSlope);
  }

  DEBUG_EEP("saved: maxHz=%u maxSpd=%u offset=%u mph=%u calVal=%u | fbEn=%u fbMaxHz=%u Kp=%.2f Ki=%.2f Kd=%.2f",
            (unsigned)maxFreqHall, (unsigned)maxSpeed, (unsigned)speedOffset, (unsigned)convertToMPH,
            (unsigned)motorPerformanceVal, (unsigned)feedbackEnable, (unsigned)feedbackMaxFreq,
            pidKp, pidKi, pidKd);
}
