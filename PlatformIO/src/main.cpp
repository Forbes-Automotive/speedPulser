#include "Arduino.h"
#include "speedPulser_defs.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include "speedPulser_tasks.h"
#include "speedPulser_calBuilder.h"
#include "speedPulser_voltage.h"
#include "power_manager.h"
#include "wifi_manager.h"
#include "speedPulser_resetLog.h"

/*
SpeedPulser - Forbes Automotive '26
Analog speed converter suitable for mechanical drive speedometers.

Tested on VW, Ford, Fiat, Mercedes & Jaguar clusters.

Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor and
converts it into a PWM signal for a BLDC motor using FreeRTOS multitasking.

Built on:
- native LEDC hardware PWM (driver/ledc.h)
- FreeRTOS tasks: the 100 ms control task, the settings save task, power management
- the speed sensor timed pulse by pulse and averaged (speedPulser_control.cpp)
- AsyncWebServer + a JSON API for the web UI
*/

void setup()
{
    // Stop the motor drive IMMEDIATELY: GPIO2 floats at reset
    // so drive it low to stop random motor movement on boot  
    pinMode(pinMotorOutput, OUTPUT);
    digitalWrite(pinMotorOutput, LOW);

    // Sample the board-version strap (GPIO3) before anything else uses the pin.
    detectBoardVersion();

#if enableDebug || defined(BENCH_SERIAL)
    Serial.begin(baudSerial);
    Serial.setTxTimeoutMs(10); // non-blocking TX: don't stall if no USB host is connected
    // Wait for the native USB-CDC to start
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 2000)
    {
        delay(10);
    }
    DEBUG("====================================================");
    DEBUG(" SpeedPulser v%s  —  Forbes Automotive", VERSION);
    DEBUG(" FreeRTOS + LEDC + AsyncWebServer");
    DEBUG(" build: %s %s", __DATE__, __TIME__);
    DEBUG("====================================================");
#endif

    // Log WHY this start happened (power-on / brown-out / crash / watchdog...) before
    // anything else can fail, so a supply that drops out while cranking shows up in the app.
    resetLogInit();

    readEEP();          // Load saved settings from EEPROM
    calBuilderInit();   // Load any user (SpeedPulserV2) custom calibration 
    updateMotorArray(); // Update motor calibration array from EEPROM

    basicInit();     // Initialize GPIO, LEDC PWM, interrupts
    setMotorDutyRaw(0); // motor off

    // Voltage board only: bring up the adjustable motor supply, starting at the
    // minimum (safe) rail with the motor PWM already off.
    initVoltageControl();
    if (boardHasVoltageControl)
        enableBuck();

    connectWifi();    // Enable WiFi and start AP/Station
    setupWebServer(); // Setup API web server and serve web files

#ifndef BENCH_SERIAL
    if (needleSweepOnBoot)
    {
        needleSweep(); // Run needle sweep if enabled
    }
#endif

    // Universal reduced-power module (power_manager): 
    // turns WiFi off 1 min after the last client
    // disconnects then scales CPU 160->80 MHz, turns off Bluetooth and cuts current
    // draw through the system. Auto-detects LOLIN-C3 at compile time
    // (160 MHz cap, no LED pin).
    power_config_t pcfg = powerDefaultConfig();
    pcfg.verbose = (enableDebug && debugPower);
    powerInit(&pcfg);

    taskInit(); // start FreeRTOS tasks for speed control, EEPROM management
#ifdef BENCH_SERIAL
    benchInit(); // serial bench interface (bench build only)
#endif

    DEBUG("setup complete — %s board, maxSpeed=%u km/h, %.0f pulses/km, dial in %s. Running.",
          boardHasVoltageControl ? "voltage" : "legacy", (unsigned)maxSpeed, sensorPulsesPerKm,
          clusterInMph ? "mph" : "km/h");
}

void loop()
{
    // all work is done in tasks - this is just a placeholder
    // to keep the Arduino framework happy
    vTaskDelay(pdMS_TO_TICKS(1000));
    resetLogTick();    // marks this start "ran OK" once it has run 10 s
    wifiManagerTick(); // Home WiFi (bridge mode): connection tracking + retry back-off
}
