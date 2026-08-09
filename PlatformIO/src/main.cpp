#include "Arduino.h"
#include "speedPulser_defs.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include "speedPulser_tasks.h"
#include "speedPulser_calBuilder.h"
#include "power_manager.h"

/*
SpeedPulser - Forbes Automotive '26
Analog speed converter suitable for mechanical drive speedometers.

Tested on VW, Ford, Fiat, Mercedes & Jaguar clusters.

Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor and
converts it into a PWM signal for a BLDC motor using FreeRTOS multitasking.

Refactored to use:
- Native LEDC hardware PWM (driver/ledc.h) instead of ESP32_FastPWM
- FreeRTOS tasks for speed control (real-time), EEPROM, and WiFi management
- RunningMedian for capturing and filtering input pulses
- AsyncWebServer with API instead of ESPUI
- Global speed offset changed to curve with linear interpolation
*/

void setup()
{
    // Stop the motor drive IMMEDIATELY: GPIO2 floats at reset
    // so drive it low to stop random motor movement on boot  
    pinMode(pinMotorOutput, OUTPUT);
    digitalWrite(pinMotorOutput, LOW);

#if enableDebug
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

    readEEP();          // Load saved settings from EEPROM
    calBuilderInit();   // Load any user (SpeedPulserV2) custom calibration 
    updateMotorArray(); // Update motor calibration array from EEPROM

    basicInit();     // Initialize GPIO, LEDC PWM, interrupts
    setMotorDuty(0); // Motor off initially

    connectWifi();    // Enable WiFi and start AP/Station
    setupWebServer(); // Setup API web server and serve web files

    if (hasNeedleSweep)
    {
        needleSweep(); // Run needle sweep if enabled
    }

    // Universal reduced-power module (power_manager): 
    // turns WiFi off 1 min after the last client
    // disconnects then scales CPU 160->80 MHz, turns off Bluetooth and cuts current
    // draw through the system. Auto-detects LOLIN-C3 at compile time
    // (160 MHz cap, no LED pin).
    power_config_t pcfg = powerDefaultConfig();
    pcfg.verbose = (enableDebug && debugPower);
    powerInit(&pcfg);

    taskInit(); // start FreeRTOS tasks for speed control, EEPROM management

    DEBUG("setup complete — cal=%u maxSpeed=%u kph units=%s feedback=%s. Running.",
          (unsigned)motorPerformanceVal, (unsigned)maxSpeed,
          convertToMPH ? "mph" : "kph", feedbackEnable ? "ON" : "OFF");
}

void loop()
{
    // all work is done in tasks - this is just a placeholder
    // to keep the Arduino framework happy
    vTaskDelay(pdMS_TO_TICKS(1000));
}
