#include "speedPulser_defs.h"

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
  pref.begin("reverseDir", false);
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
    pref.putBool("reverseDir", reverseDirection);
    pref.putBool("fbEnable", feedbackEnable);
    pref.putFloat("pidKp", pidKp);
    pref.putFloat("pidKi", pidKi);
    pref.putFloat("pidKd", pidKd);
    pref.putFloat("fbDeadband", feedbackDeadband);
    pref.putUShort("fbMinSpd", feedbackMinSpeed);
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
    reverseDirection = pref.getBool("reverseDir", false);
    feedbackEnable = pref.getBool("fbEnable", false);
    pidKp = pref.getFloat("pidKp", 0.15f);
    pidKi = pref.getFloat("pidKi", 1.3f);
    pidKd = pref.getFloat("pidKd", 0.0f);
    feedbackDeadband = pref.getFloat("fbDeadband", 1.5f);
    feedbackMinSpeed = pref.getUShort("fbMinSpd", 40);
  }

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
  pref.putBool("reverseDir", reverseDirection);
  pref.putBool("fbEnable", feedbackEnable);
  pref.putFloat("pidKp", pidKp);
  pref.putFloat("pidKi", pidKi);
  pref.putFloat("pidKd", pidKd);
  pref.putFloat("fbDeadband", feedbackDeadband);
  pref.putUShort("fbMinSpd", feedbackMinSpeed);

  DEBUG_EEP("saved: maxHz=%u maxSpd=%u offset=%u mph=%u calVal=%u | fbEn=%u fbMaxHz=%u Kp=%.2f Ki=%.2f Kd=%.2f",
            (unsigned)maxFreqHall, (unsigned)maxSpeed, (unsigned)speedOffset, (unsigned)convertToMPH,
            (unsigned)motorPerformanceVal, (unsigned)feedbackEnable, (unsigned)feedbackMaxFreq,
            pidKp, pidKi, pidKd);
}
