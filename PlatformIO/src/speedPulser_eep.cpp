#include "speedPulser_defs.h"

void readEEP() {
#if serialDebug
  DEBUG_PRINTLN("EEPROM initialising!");
#endif

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

  // first run comes with EEP valve of 255, so write actual values.  If found/match SW version, read all the values
  if (pref.getUChar("testSpeedo") == 255) {
#if serialDebug
    DEBUG_PRINTLN("First run, set Bluetooth module, write Software Version etc");
    DEBUG_PRINTLN(pref.getUChar("testSpeedo"));
#endif
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
  } else {
    hasNeedleSweep = pref.getBool("hasNeedleSweep", false);
    testSpeedo = pref.getBool("testSpeedo", false);
    speedOffsetPositive = pref.getBool("offsetPositive", true);
    tempSpeed = pref.getUShort("tempSpeed", 100);
    maxFreqHall = pref.getUShort("maxFreqHall", 200);
    maxSpeed = pref.getUShort("maxSpeed", 200);
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
  }

  normaliseSpeedOffsetCurve();
#if serialDebug
  DEBUG_PRINTLN("EEPROM initialised with...");
  DEBUG_PRINTLN(hasNeedleSweep);
  DEBUG_PRINTLN(testSpeedo);
  DEBUG_PRINTLN(speedOffsetPositive);
  DEBUG_PRINTLN(tempSpeed);
  DEBUG_PRINTLN(maxFreqHall);
  DEBUG_PRINTLN(maxSpeed);
  DEBUG_PRINTLN(speedOffset);
  DEBUG_PRINTLN(convertToMPH);
  DEBUG_PRINTLN(motorPerformanceVal);
  DEBUG_PRINTLN(sweepSpeed);
  DEBUG_PRINTLN(useSpeedOffsetCurve);
#endif
}

void writeEEP() {
#if serialDebug
  DEBUG_PRINTLN("Writing EEPROM...");
#endif

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

#if serialDebug
  DEBUG_PRINTLN("Written EEPROM with data:...");
  DEBUG_PRINTLN(hasNeedleSweep);
  DEBUG_PRINTLN(testSpeedo);
  DEBUG_PRINTLN(speedOffsetPositive);
  DEBUG_PRINTLN(tempSpeed);
  DEBUG_PRINTLN(maxFreqHall);
  DEBUG_PRINTLN(maxSpeed);
  DEBUG_PRINTLN(speedOffset);
  DEBUG_PRINTLN(convertToMPH);
  DEBUG_PRINTLN(motorPerformanceVal);
  DEBUG_PRINTLN(sweepSpeed);
  DEBUG_PRINTLN(useSpeedOffsetCurve);
#endif
}
