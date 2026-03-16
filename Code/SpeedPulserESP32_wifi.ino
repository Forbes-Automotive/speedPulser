void setupUI() {
  //Turn off verbose debugging
  ESPUI.setVerbosity(Verbosity::Quiet);
  ESPUI.sliderContinuous = true;

  // create basic tab
  auto tabBasic = ESPUI.addControl(Tab, "", "Basic Controls");
  ESPUI.addControl(Separator, "Needle Sweep", "", Dark, tabBasic);
  bool_NeedleSweep = ESPUI.addControl(Switcher, "Needle Sweep", String(hasNeedleSweep), Dark, tabBasic, generalCallback);
  int16_sweepSpeed = ESPUI.addControl(Slider, "Rate of Change (ms)", String(sweepSpeed), Dark, tabBasic, generalCallback);
  ESPUI.addControl(Min, "", "0", Dark, int16_sweepSpeed);
  ESPUI.addControl(Max, "", "50", Dark, int16_sweepSpeed);
  ESPUI.addControl(Button, "Test Needle Sweep", "Test", Dark, tabBasic, extendedCallback, (void *)11);

  // create advanced tab
  auto tabAdvanced = ESPUI.addControl(Tab, "", "Advanced Controls");
  ESPUI.addControl(Separator, "Calibration:", "", Dark, tabAdvanced);
  int16_calNumber = ESPUI.addControl(Select, "Calibration", "", Dark, tabAdvanced, generalCallback);
  ESPUI.addControl(Option, "VW - 120mph; Martin Springell", "VW120Martin", Dark, int16_calNumber);
  ESPUI.addControl(Option, "VW - 120mph; Forbes-Automotive", "VW120Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "VW - 140mph; Forbes-Automotive", "VW140Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "VW - 160mph; Forbes-Automotive", "VW160Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "VW - 300kph; Forbes-Automotive", "VW300Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "Ford - 120mph_1; Forbes-Automotive", "Ford120Forbes1", Dark, int16_calNumber);
  ESPUI.addControl(Option, "Ford - 120mph_2; Forbes-Automotive", "Ford120Forbes2", Dark, int16_calNumber);
  ESPUI.addControl(Option, "FIAT - 40-160mph; Forbes-Automotive", "FIAT160Forbes1", Dark, int16_calNumber);
  ESPUI.addControl(Option, "FIAT - 20-110mph; Forbes-Automotive", "FIAT160Forbes2", Dark, int16_calNumber);
  ESPUI.addControl(Option, "Merc - 120mph; Forbes-Automotive", "Merc120Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "Smiths - 70mph; Forbes-Automotive", "Smiths70Forbes", Dark, int16_calNumber);
  ESPUI.addControl(Option, "Smiths - 90mph; Forbes-Automotive", "Smiths90Forbes", Dark, int16_calNumber);  // purely for future cals so WiFi is already pre-defined
  ESPUI.addControl(Option, "Smiths - 150mph; Forbes-Automotive", "Smiths150Forbes", Dark, int16_calNumber);          // purely for future cals so WiFi is already pre-defined
  ESPUI.addControl(Option, "FutureCal3; Forbes-Automotive", "FutureCal3", Dark, int16_calNumber);          // purely for future cals so WiFi is already pre-defined

  ESPUI.addControl(Separator, "Testing", "", Dark, tabAdvanced);
  bool_testSpeedo = ESPUI.addControl(Switcher, "Test Speedo", "", Dark, tabAdvanced, generalCallback);
  int16_tempSpeed = ESPUI.addControl(Slider, "Go to Speed", String(tempSpeed), Dark, tabAdvanced, generalCallback);
  ESPUI.addControl(Min, "", "0", Dark, int16_tempSpeed);
  ESPUI.addControl(Max, "", "400", Dark, int16_tempSpeed);

  ESPUI.addControl(Separator, "Speed Offsets", "", Dark, tabAdvanced);
  bool_positiveOffset = ESPUI.addControl(Switcher, "Positive Offset", String(speedOffsetPositive), Dark, tabAdvanced, generalCallback);
  int16_speedOffset = ESPUI.addControl(Slider, "Speed Offset", String(speedOffset), Dark, tabAdvanced, generalCallback);
  ESPUI.addControl(Min, "", "0", Dark, int16_speedOffset);
  ESPUI.addControl(Max, "", "50", Dark, int16_speedOffset);

  ESPUI.addControl(Separator, "Speed Limits:", "", Dark, tabAdvanced);
  int16_maxSpeed = ESPUI.addControl(Slider, "Maximum Speed", String(maxSpeed), Dark, tabAdvanced, generalCallback);
  ESPUI.addControl(Max, "", "400", Dark, int16_maxSpeed);
  ESPUI.addControl(Button, "Reset", "Reset", Dark, tabAdvanced, extendedCallback, (void *)12);

  ESPUI.addControl(Separator, "Hall Incoming Freq:", "", Dark, tabAdvanced);
  int16_maxHall = ESPUI.addControl(Slider, "Maximum Hall", String(maxFreqHall), Dark, tabAdvanced, generalCallback);
  ESPUI.addControl(Max, "", "400", Dark, int16_maxHall);
  ESPUI.addControl(Button, "Reset", "Reset", Dark, tabAdvanced, extendedCallback, (void *)13);

  ESPUI.addControl(Separator, "Incoming Speed:", "", Dark, tabAdvanced);
  label_speed = ESPUI.addControl(Label, "", "0", Dark, tabAdvanced, generalCallback);

  // create calibration tab:
  auto tabCal = ESPUI.addControl(Tab, "", "Calibration");
  ESPUI.addControl(Separator, "Calibration:", "", Dark, tabCal);
  bool_testCal = ESPUI.addControl(Switcher, "Enable Calibration", "", Dark, tabCal, generalCallback);
  ESPUI.addControl(Button, "Previous Duty", "Previous", Dark, tabCal, extendedCallback, (void *)14);
  ESPUI.addControl(Button, "Next Duty", "Next", Dark, tabCal, extendedCallback, (void *)15);
  label_currentPWM = ESPUI.addControl(Label, "", "0", Dark, tabCal, generalCallback);

  //Finally, start up the UI.
  //This should only be called once we are connected to WiFi.
  ESPUI.begin(wifiHostName);
}

void updateCallback(Control *sender, int type) {
  updates = (sender->value.toInt() > 0);
}

void getTimeCallback(Control *sender, int type) {
  if (type == B_UP) {
    ESPUI.updateTime(mainTime);
  }
}

void graphAddCallback(Control *sender, int type) {
  if (type == B_UP) {
    ESPUI.addGraphPoint(graph, random(1, 50));
  }
}

void graphClearCallback(Control *sender, int type) {
  if (type == B_UP) {
    ESPUI.clearGraph(graph);
  }
}

void generalCallback(Control *sender, int type) {
#if serialDebugWifi
  Serial.print("CB: id(");
  Serial.print(sender->id);
  Serial.print(") Type(");
  Serial.print(type);
  Serial.print(") '");
  Serial.print(sender->label);
  Serial.print("' = ");
  Serial.println(sender->value);
#endif

  uint8_t tempID = int(sender->id);
  switch (tempID) {
    case 3: hasNeedleSweep = sender->value.toInt(); break;
    case 4: sweepSpeed = sender->value.toInt(); break;
    case 10:
      if (sender->value == "VW120Martin") motorPerformanceVal = 1;
      if (sender->value == "VW120Forbes") motorPerformanceVal = 2;
      if (sender->value == "VW140Forbes") motorPerformanceVal = 3;
      if (sender->value == "VW160Forbes") motorPerformanceVal = 4;
      if (sender->value == "VW300Forbes") motorPerformanceVal = 5;
      if (sender->value == "Ford120Forbes1") motorPerformanceVal = 6;
      if (sender->value == "Ford120Forbes2") motorPerformanceVal = 7;
      if (sender->value == "FIAT160Forbes1") motorPerformanceVal = 8;
      if (sender->value == "FIAT160Forbes2") motorPerformanceVal = 9;
      if (sender->value == "Merc120Forbes") motorPerformanceVal = 10;
      if (sender->value == "Smiths70Forbes") motorPerformanceVal = 11;
      if (sender->value == "Smiths90Forbes") motorPerformanceVal = 12;
      if (sender->value == "Smiths150Forbes") motorPerformanceVal = 13;

      if (sender->value == "FutureCal3") motorPerformanceVal = 14;

      updateMotorPerformance = true;
      break;
    case 26: testSpeedo = sender->value.toInt(); break;
    case 27: tempSpeed = sender->value.toInt(); break;
    case 31: speedOffsetPositive = sender->value.toInt(); break;
    case 32: speedOffset = sender->value.toInt(); break;
    case 36: maxSpeed = sender->value.toInt(); break;
    case 40: maxFreqHall = sender->value.toInt(); break;
    case 47: testCal = sender->value.toInt(); break;
  }
}

void extendedCallback(Control *sender, int type, void *param) {
#if serialDebugWifi
  Serial.print("CB: id(");
  Serial.print(sender->id);
  Serial.print(") Type(");
  Serial.print(type);
  Serial.print(") '");
  Serial.print(sender->label);
  Serial.print("' = ");
  Serial.println(sender->value);
  Serial.print("param = ");
  Serial.println((long)param);
#endif

  uint8_t tempID = int(sender->id);
  switch (tempID) {
    case 7:
      if (type == B_UP) {
        testNeedleSweep = true;
      }
      break;
    case 38:
      if (type == B_UP) {
        maxSpeed = 200;
        ESPUI.updateSlider(int16_maxSpeed, maxSpeed);
      }
      break;
    case 42:
      if (type == B_UP) {
        maxFreqHall = 200;
        ESPUI.updateSlider(int16_maxHall, maxFreqHall);
      }
      break;
    case 48:
      if (type == B_UP) {
        tempDutyCycle = tempDutyCycle - 1;
        if (tempDutyCycle < 0) {
          tempDutyCycle = 385;
        }
      }
      break;
    case 49:
      if (type == B_UP) {
        tempDutyCycle = tempDutyCycle + 1;
        if (tempDutyCycle > 385) {
          tempDutyCycle = 0;
        }
      }
      break;
  }
}

void connectWifi() {
  WiFi.hostname(wifiHostName);

#if serialDebugWifi
  DEBUG_PRINTLN("Starting WiFi...");
  DEBUG_PRINTLN("Creating access point...");
#endif

  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(wifiHostName);
}

void textCallback(Control *sender, int type) {
  //This callback is needed to handle the changed values, even though it doesn't do anything itself.
}

void randomString(char *buf, int len) {
  for (auto i = 0; i < len - 1; i++)
    buf[i] = random(0, 26) + 'A';
  buf[len - 1] = '\0';
}

void disconnectWifi() {
#if serialDebugWifi
  DEBUG_PRINTF("Number of connections: ");
  DEBUG_PRINTLN(WiFi.softAPgetStationNum());
#endif

  if (WiFi.softAPgetStationNum() == 0) {
#if serialDebugWifi
    DEBUG_PRINTLN("No connections, turning off");
#endif

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
}

void updateLabels() {
  switch (motorPerformanceVal) {
    case 1:
      ESPUI.updateSelect(int16_calNumber, "VW120Martin");
      break;
    case 2:
      ESPUI.updateSelect(int16_calNumber, "VW120Forbes");
      break;
    case 3:
      ESPUI.updateSelect(int16_calNumber, "VW140Forbes");
      break;
    case 4:
      ESPUI.updateSelect(int16_calNumber, "VW160Forbes");
      break;
    case 5:
      ESPUI.updateSelect(int16_calNumber, "VW300Forbes");
      break;
    case 6:
      ESPUI.updateSelect(int16_calNumber, "Ford120Forbes1");
      break;
    case 7:
      ESPUI.updateSelect(int16_calNumber, "Ford120Forbes2");
      break;
    case 8:
      ESPUI.updateSelect(int16_calNumber, "FIAT160Forbes1");
      break;
    case 9:
      ESPUI.updateSelect(int16_calNumber, "FIAT160Forbes2");
      break;
    case 10:
      ESPUI.updateSelect(int16_calNumber, "Merc120Forbes");
      break;
    case 11:
      ESPUI.updateSelect(int16_calNumber, "Smiths70Forbes");
      break;
    case 12:
      ESPUI.updateSelect(int16_calNumber, "Smiths90Forbes");
      break;
    case 13:
      ESPUI.updateSelect(int16_calNumber, "Smiths150Forbes");
      break;
    case 14:
      ESPUI.updateSelect(int16_calNumber, "Smiths70Forbes");
      break;
  }
}

void setupOTA() {
  updateServer.setup(ESPUI.WebServer(), "", "");

  updateServer.onUpdateBegin = [](const UpdateType type, int &result) {
    //you can force abort the update like this if you need to:
    //result = UpdateResult::UPDATE_ABORT;
    Serial.println("Update started : " + String(type));
  };
  updateServer.onUpdateEnd = [](const UpdateType type, int &result) {
    Serial.println("Update finished : " + String(type) + " result: " + String(result));
  };
}