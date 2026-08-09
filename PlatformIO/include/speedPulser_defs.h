#ifndef SPEED_PULSER_DEFS_H
#define SPEED_PULSER_DEFS_H

#include <Arduino.h>
#include <driver/ledc.h>   // for LEDC PWM (hardware PWM)
#include <RunningMedian.h> // for calculating median
#include <Preferences.h>   // for eeprom/remember settings
#include <WiFi.h>          // for WiFi interface
#include <ESPmDNS.h>       // for WiFi interface
#include <LittleFS.h>      // for serving static files

// for Web Server
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> // for UI/API JSON handling

// Create global AsyncWebServer instance
extern AsyncWebServer server;

// ---------------------------------------------------------------------------
// Serial debug — output per major subsystem
//
//   * enableDebug is the SINGLE master control. Set it to 0 to silence EVERY
//     serial debug statement.
//   * With enableDebug = 1, toggle the per-subsystem flags below to pick which
//     tagged Serial streams you want.
//   * Every subsystem gets DEBUG_xxx(...) (adds a newline) and DEBUG_xxx_(...)
//     (no newline) printf-style macros, each prefixed with a [TAG] so the log
//     is neat and easy to follow.
// ---------------------------------------------------------------------------
#define enableDebug 0 // ** Main ** 0 = silence ALL serial debug

#define debugSys 1   // [SYS]  boot / general / 1 Hz loop
#define debugPower 1 // [PWR]  power_manager (reduced power / wake)
#define debugWifi 1  // [WiFi] soft-AP + web server bring-up
#define debugIO 1    // [IO]   GPIO / LEDC PWM / interrupt init
#define debugEEP 1   // [EEP]  EEPROM / Preferences load & save
#define debugCtrl 1  // [CTRL] speed input / duty mapping
#define debugFB 1    // [FB]   closed-loop feedback / PID diagnostics
#define debugWeb 1   // [WEB]  web server / API / UI / OTA

#ifndef baudSerial
#define baudSerial 115200 // serial monitor baud rate
#endif

// --- [SYS] master / general (uncategorised) ---
#if enableDebug && debugSys
#define DEBUG(x, ...) Serial.printf("[SYS] " x "\n", ##__VA_ARGS__)
#define DEBUG_(x, ...) Serial.printf("[SYS] " x, ##__VA_ARGS__)
#else
#define DEBUG(x, ...)
#define DEBUG_(x, ...)
#endif

// --- [PWR] power management ---
#if enableDebug && debugPower
#define DEBUG_PWR(x, ...) Serial.printf("[PWR] " x "\n", ##__VA_ARGS__)
#define DEBUG_PWR_(x, ...) Serial.printf("[PWR] " x, ##__VA_ARGS__)
#else
#define DEBUG_PWR(x, ...)
#define DEBUG_PWR_(x, ...)
#endif

// --- [WiFi] soft-AP + web server ---
#if enableDebug && debugWifi
#define DEBUG_WIFI(x, ...) Serial.printf("[WiFi] " x "\n", ##__VA_ARGS__)
#define DEBUG_WIFI_(x, ...) Serial.printf("[WiFi] " x, ##__VA_ARGS__)
#else
#define DEBUG_WIFI(x, ...)
#define DEBUG_WIFI_(x, ...)
#endif

// --- [IO] GPIO / LEDC PWM / interrupt init ---
#if enableDebug && debugIO
#define DEBUG_IO(x, ...) Serial.printf("[IO] " x "\n", ##__VA_ARGS__)
#define DEBUG_IO_(x, ...) Serial.printf("[IO] " x, ##__VA_ARGS__)
#else
#define DEBUG_IO(x, ...)
#define DEBUG_IO_(x, ...)
#endif

// --- [EEP] EEPROM / Preferences ---
#if enableDebug && debugEEP
#define DEBUG_EEP(x, ...) Serial.printf("[EEP] " x "\n", ##__VA_ARGS__)
#define DEBUG_EEP_(x, ...) Serial.printf("[EEP] " x, ##__VA_ARGS__)
#else
#define DEBUG_EEP(x, ...)
#define DEBUG_EEP_(x, ...)
#endif

// --- [CTRL] speed input / duty mapping ---
#if enableDebug && debugCtrl
#define DEBUG_CTRL(x, ...) Serial.printf("[CTRL] " x "\n", ##__VA_ARGS__)
#define DEBUG_CTRL_(x, ...) Serial.printf("[CTRL] " x, ##__VA_ARGS__)
#else
#define DEBUG_CTRL(x, ...)
#define DEBUG_CTRL_(x, ...)
#endif

// --- [FB] closed-loop feedback / PID diagnostics ---
#if enableDebug && debugFB
#define DEBUG_FB(x, ...) Serial.printf("[FB] " x "\n", ##__VA_ARGS__)
#define DEBUG_FB_(x, ...) Serial.printf("[FB] " x, ##__VA_ARGS__)
#else
#define DEBUG_FB(x, ...)
#define DEBUG_FB_(x, ...)
#endif

// --- [WEB] web server / API / UI / OTA ---
#if enableDebug && debugWeb
#define DEBUG_WEB(x, ...) Serial.printf("[WEB] " x "\n", ##__VA_ARGS__)
#define DEBUG_WEB_(x, ...) Serial.printf("[WEB] " x, ##__VA_ARGS__)
#else
#define DEBUG_WEB(x, ...)
#define DEBUG_WEB_(x, ...)
#endif

// --- Legacy Serial: kept so any un-migrated call site still compiles, routed
//     through the master. Prefer to use the tagged DEBUG_xxx macros above. ---
#if enableDebug
extern bool serialActive; // true only when a Serial host is connected
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTF(...)
#define DEBUG_PRINTLN(x)
#endif

// Legacy for the old flag names (in case anything still tests them).
#ifndef serialDebug
#define serialDebug (enableDebug && debugSys)
#endif
#ifndef serialDebugWifi
#define serialDebugWifi (enableDebug && debugWifi)
#endif

#define eepRefresh 2000   // EEPROM Refresh in ms
#define wifiDisable 60000 // turn off WiFi in ms

// ===== Testing and Configuration Variables =====
#define durationReset 1500 // duration of 'last sample' before reset speed back to zero

extern bool testSpeedo;     // test speedo on boot (UI toggle)
extern bool testCal;        // calibration test on boot (UI toggle)
extern bool hasNeedleSweep; // needle sweep test on boot (UI toggle)

extern uint8_t sweepSpeed;    // needle sweep speed (UI slider, 1-1000 ms)
extern uint8_t averageFilter; // sample count for median filter (1-10)

// ===== Motor Performance and Calibration =====
extern bool updateMotorPerformance; // set true when motorPerformanceVal is changed and the array needs updating

extern uint16_t motorPerformance[385]; // current selected motor calibration array (updated from motorPerformanceVal)
extern uint8_t motorPerformanceVal;    // current selected motor calibration ID (0-14: 0=custom, 1-14=predefined)

// ===== Speed Control Configuration =====
#define speedMultiplier 1

extern uint16_t maxFreqHall;
extern uint16_t maxSpeed;
extern uint8_t speedOffset;
extern bool speedOffsetPositive;
extern bool convertToMPH;
extern bool useSpeedOffsetCurve;

#define SPEED_OFFSET_CURVE_POINTS 5
extern int16_t speedOffsetCurveOffsets[SPEED_OFFSET_CURVE_POINTS];

// ===== Motor Direction & PID Feedback =====
extern bool reverseDirection;     // UI toggle; drives pinDirection HIGH to reverse
extern bool feedbackEnable;       // UI toggle; enable PID speed feedback trim on GPIO4
extern float pidKp;               // PID proportional gain
extern float pidKi;               // PID integral gain
extern float pidKd;               // PID derivative gain
extern float feedbackDeadband;    // PID deadband (Hz); within this error P/D are silenced (integral still trims). 0 = off
extern uint16_t feedbackMaxFreq;  // feedback pulses/sec that correspond to maxSpeed
extern uint16_t feedbackMinSpeed; // kph; below this target the loop runs open-loop (anti-hunt)
extern uint16_t measuredSpeed;    // measured motor speed from feedback (kph)
extern int16_t pidCorrection;     // current PID duty correction (debug/UI)
extern bool feedbackAvailable;    // true once a real tacho signal (GPIO4) has been seen this session
extern bool feedbackMissing;      // true when the motor runs but no feedback signal is present (legacy PCB)

// ===== Test Speed Variable =====
extern uint16_t tempSpeed;

#define mphFactor 0.621371

#define pinMotorOutput 2 // pin for motor PWM output - needs stepped up to 5v for the motor (NPN transistor on the board).  Needs to support LED PWM(!)
#define pinSpeedInput 5  // interrupt supporting pin for speed input.  ESP32 C3 doesn't like them all, so if changing test this first(!)
#define pinDirection 10  // motor direction pin: HIGH reverses the motor, LOW = normal (default)
#define pinFeedback 4    // motor speed feedback pulse input (interrupt counter; C3 has no PCNT)
#define pinOnboardLED 8  // for feedback for input checking / flash LED on input.  ESP32 C3 is Pin 8

#define wifiHostName "SpeedPulser" // the WiFi name

// Global variables - moved to respective modules
extern Preferences pref;

// Callbacks and functions from other modules
void incomingHz(void);                             // interrupt handler - defined in control.cpp
void feedbackPulse(void);                          // feedback interrupt handler - defined in control.cpp
void applyDirection(void);                         // drive direction pin from reverseDirection - io.cpp
void resetPid(void);                               // reset PID state - control.cpp
uint16_t findClosestMatch(uint16_t val);           // speed matching - defined in control.cpp
int16_t getCurveOffsetForSpeed(uint16_t speedKph); // fixed-range offset from 5-point curve
uint16_t applyConfiguredSpeedOffset(uint16_t speedKph);
void normaliseSpeedOffsetCurve();

// Function declarations - I/O module
void basicInit();
void setMotorDuty(uint32_t duty);       // duty in the 10-bit calibration domain (scaled to PWM internally)
void setMotorDutyRaw(uint32_t pwmDuty); // duty written straight to the PWM hardware domain (0..(1<<PWM_RESOLUTION)-1)
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

#endif // SPEED_PULSER_DEFS_H
