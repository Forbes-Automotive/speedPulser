void readEEP() {
#if serialDebug
  DEBUG_PRINTLN("EEPROM initialising!");
#endif

  // use ESP32's 'Preferences' to remember settings.  Begin by opening the various types.  Use 'false' for read/write.  True just gives read access
  pref.begin("hasNeedleSweep", false);
  pref.begin("testSpeedo", false);
  pref.begin("speedOffsetPositive", false);
  pref.begin("tempSpeed", false);
  pref.begin("maxFreqHall", false);
  pref.begin("maxSpeed", false);
  pref.begin("speedOffset", false);
  pref.begin("motorPerfVal", false);
  pref.begin("sweepSpeed", false);

  // first run comes with EEP valve of 255, so write actual values.  If found/match SW version, read all the values
  if (pref.getUChar("testSpeedo") == 255) {
#if serialDebug
    DEBUG_PRINTLN("First run, set Bluetooth module, write Software Version etc");
    DEBUG_PRINTLN(pref.getUChar("testSpeedo"));
#endif
    pref.putBool("hasNeedleSweep", hasNeedleSweep);
    pref.putBool("testSpeedo", testSpeedo);
    pref.putBool("speedOffsetPositive", speedOffsetPositive);
    pref.putUShort("tempSpeed", tempSpeed);
    pref.putUShort("maxFreqHall", maxFreqHall);
    pref.putUShort("maxSpeed", maxSpeed);
    pref.putUShort("speedOffset", speedOffset);
    pref.putUChar("motorPerfVal", motorPerformanceVal);
    pref.putUChar("sweepSpeed", sweepSpeed);
  } else {
    hasNeedleSweep = pref.getBool("hasNeedleSweep", false);
    testSpeedo = pref.getBool("testSpeedo", false);
    speedOffsetPositive = pref.getBool("speedOffsetPositive", 0);
    tempSpeed = pref.getUShort("tempSpeed", 100);
    maxFreqHall = pref.getUShort("maxFreqHall", 200);
    maxSpeed = pref.getUShort("maxSpeed", 200);
    speedOffset = pref.getUShort("speedOffset", 0);
    motorPerformanceVal = pref.getUChar("motorPerfVal", 0);
    sweepSpeed = pref.getUChar("sweepSpeed", 18);
  }
#if serialDebug
  DEBUG_PRINTLN("EEPROM initialised with...");
  DEBUG_PRINTLN(hasNeedleSweep);
  DEBUG_PRINTLN(testSpeedo);
  DEBUG_PRINTLN(speedOffsetPositive);
  DEBUG_PRINTLN(tempSpeed);
  DEBUG_PRINTLN(maxFreqHall);
  DEBUG_PRINTLN(maxSpeed);
  DEBUG_PRINTLN(speedOffset);
  DEBUG_PRINTLN(motorPerformanceVal);
  DEBUG_PRINTLN(sweepSpeed);
#endif
}

void writeEEP() {
#if serialDebug
  DEBUG_PRINTLN("Writing EEPROM...");
#endif

  // update EEP only if changes have been made
  pref.putBool("hasNeedleSweep", hasNeedleSweep);
  pref.putBool("testSpeedo", testSpeedo);
  pref.putBool("speedOffsetPositive", speedOffsetPositive);
  pref.putUShort("tempSpeed", tempSpeed);
  pref.putUShort("maxFreqHall", maxFreqHall);
  pref.putUShort("maxSpeed", maxSpeed);
  pref.putUShort("speedOffset", speedOffset);
  pref.putUChar("motorPerfVal", motorPerformanceVal);
  pref.putUChar("sweepSpeed", sweepSpeed);

#if serialDebug
  DEBUG_PRINTLN("Written EEPROM with data:...");
  DEBUG_PRINTLN(hasNeedleSweep);
  DEBUG_PRINTLN(testSpeedo);
  DEBUG_PRINTLN(speedOffsetPositive);
  DEBUG_PRINTLN(tempSpeed);
  DEBUG_PRINTLN(maxFreqHall);
  DEBUG_PRINTLN(maxSpeed);
  DEBUG_PRINTLN(speedOffset);
  DEBUG_PRINTLN(motorPerformanceVal);
  DEBUG_PRINTLN(sweepSpeed);
#endif
}
