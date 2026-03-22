#include "Arduino.h"
#include "speedPulser_defs.h"
#include "speedPulser_ver.h"
#include "speedPulser_control.h"
#include "speedPulser_tasks.h"

/*
SpeedPulser - Forbes Automotive '26
Analog speed converter suitable for mechanical drive speedometers. Tested on VW, Ford & Fiat clusters.

Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor and converts it into
a PWM signal for a BLDC motor using FreeRTOS multitasking.

Refactored to use:
- Native LEDC hardware PWM (driver/ledc.h) instead of ESP32_FastPWM
- FreeRTOS tasks for speed control (real-time), EEPROM, and WiFi management
- RunningMedian for capturing and filtering input pulses
- AsyncWebServer with API instead of ESPUI
- Global speed offset changed to curve with linear interpolation
*/

void setup()
{
#if serialDebug
    Serial.begin(baudSerial);
    Serial.setTxTimeoutMs(10); // non-blocking TX: don't stall if no USB host is connected
    DEBUG_PRINTLN("Initialising SpeedPulser (FreeRTOS + LEDC + API)...");
#endif

    readEEP();          // Load saved settings from EEPROM
    updateMotorArray(); // Update motor calibration array from EEPROM

    basicInit();                      // Initialize GPIO, LEDC PWM, interrupts
    ledcWrite(LEDC_CHANNEL_MOTOR, 0); // Motor off initially
  
    connectWifi();    // Enable WiFi and start AP/Station
    setupWebServer(); // Setup REST API web server and serve web files

    if (hasNeedleSweep)
    {
        needleSweep(); // Run needle sweep calibration if enabled
    }

    taskInit(); // start FreeRTOS tasks for speed control, EEPROM management, and WiFi management
}

void loop()
{
    // all work is done in tasks now
    vTaskDelay(pdMS_TO_TICKS(1000));
}
