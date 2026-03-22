#ifndef SPEED_PULSER_DEFS_H
#define SPEED_PULSER_DEFS_H

#include <Arduino.h>
#include <driver/ledc.h>             // for LEDC PWM (hardware PWM)
#include <RunningMedian.h>           // for calculating median
#include <Preferences.h>             // for eeprom/remember settings
#include <WiFi.h>                    // for WiFi interface
#include <ESPmDNS.h>                 // for WiFi interface
#include <LittleFS.h>                // for serving static files

// for Web Server
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>             // for REST API JSON handling

// Create global AsyncWebServer instance
extern AsyncWebServer server;

#ifndef baudSerial
#define baudSerial 115200  // baud rate for serial feedback
#endif
#ifndef serialDebug
#define serialDebug 0      // for Serial feedback - disable on release(!) ** CAN CHANGE THIS **
#endif
#ifndef serialDebugWifi
#define serialDebugWifi 0  // for Serial WiFi feedback - disable on release(!) ** CAN CHANGE THIS **
#endif
#define eepRefresh 2000    // EEPROM Refresh in ms
#define wifiDisable 60000  // turn off WiFi in ms

// ===== Testing and Configuration Variables (defined in speedPulser_defs.cpp) =====
extern bool testSpeedo;
extern bool testCal;
extern bool hasNeedleSweep;
extern uint8_t sweepSpeed;

extern uint8_t averageFilter;            // sample count for median filter (1-10)
#define durationReset 1500               // duration of 'last sample' before reset speed back to zero

// ===== Motor Performance and Calibration (defined in speedPulser_defs.cpp) =====
extern uint16_t motorPerformance[385];
extern uint8_t motorPerformanceVal;
extern bool updateMotorPerformance;

// ===== Speed Control Configuration (defined in speedPulser_defs.cpp) =====
extern uint16_t maxFreqHall;
extern uint16_t maxSpeed;
extern uint8_t speedOffset;
#define speedMultiplier 1
extern bool speedOffsetPositive;
extern bool convertToMPH;
extern bool useSpeedOffsetCurve;

#define SPEED_OFFSET_CURVE_POINTS 5
extern int16_t speedOffsetCurveOffsets[SPEED_OFFSET_CURVE_POINTS];

// ===== Test Speed Variable (defined in speedPulser_defs.cpp) =====
extern uint16_t tempSpeed;

#define mphFactor 0.621371

#define pinMotorOutput 2  // pin for motor PWM output - needs stepped up to 5v for the motor (NPN transistor on the board).  Needs to support LED PWM(!)
#define pinSpeedInput 5   // interrupt supporting pin for speed input.  ESP32 C3 doesn't like them all, so if changing test this first(!)
#define pinDirection 10   // motor direction pin (currently unused) but here for future revisions
#define pinOnboardLED 8   // for feedback for input checking / flash LED on input.  ESP32 C3 is Pin 8

#define wifiHostName "SpeedPulser"  // the WiFi name

// if serialDebug is on, allow Serial talkback with runtime presence check
#if serialDebug
extern bool serialActive;  // true only when a Serial host is connected
#define DEBUG_PRINT(x)    Serial.print(x);
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__);
#define DEBUG_PRINTLN(x)  Serial.println(x);
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTF(...)
#define DEBUG_PRINTLN(x)
#endif

// Global variables - moved to respective modules
extern Preferences pref;

// Callbacks and functions from other modules
void incomingHz(void);                  // interrupt handler - defined in control.cpp
uint16_t findClosestMatch(uint16_t val); // speed matching - defined in control.cpp
int16_t getCurveOffsetForSpeed(uint16_t speedKph); // fixed-range offset from 5-point curve
uint16_t applyConfiguredSpeedOffset(uint16_t speedKph);
void normaliseSpeedOffsetCurve();

// Function declarations - I/O module
void basicInit();
void testSpeed();
void needleSweep();

// Function declarations - Web Server
#include "speedPulser_webserver.h"
void setupWebServer();

// Function declarations - WiFi module
void connectWifi();
void disconnectWifi();

// Function declarations - EEPROM module
void readEEP();
void writeEEP();

// Function declarations - Motor calibration module
void updateMotorArray();
const char *getCalibrationText(uint8_t calibrationVal);
uint8_t getCalibrationCount();

// Function declarations - Task management
void taskInit();

// Motor calibration array sizes
extern uint16_t motorPerformance1[];
extern uint16_t motorPerformance2[];
extern uint16_t motorPerformance3[];
extern uint16_t motorPerformance4[];
extern uint16_t motorPerformance5[];
extern uint16_t motorPerformance6[];
extern uint16_t motorPerformance7[];
extern uint16_t motorPerformance8[];
extern uint16_t motorPerformance9[];
extern uint16_t motorPerformance10[];
extern uint16_t motorPerformance11[];
extern uint16_t motorPerformance12[];
extern uint16_t motorPerformance13[];
extern uint16_t motorPerformance14[];

#endif  // SPEED_PULSER_DEFS_H
