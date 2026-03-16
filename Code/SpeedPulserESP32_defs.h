#define _PWM_LOGLEVEL_ 0
#include <Arduino.h>
#include "ESP32_FastPWM.h"  // for motor control
#include <RunningMedian.h>  // for calculating median
#include "TickTwo.h"        // for repeated tasks
#include <Preferences.h>    // for eeprom/remember settings
#include <ESPUI.h>          // for WiFi interface
#include <WiFi.h>           // for WiFi interface
#include <ESPmDNS.h>        // for WiFi interface

// for OTA
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncHTTPUpdateServer.h>

#define baudSerial 115200  // baud rate for serial feedback
#define serialDebug 0      // for Serial feedback - disable on release(!) ** CAN CHANGE THIS **
#define serialDebugWifi 0  // for Serial WiFi feedback - disable on release(!) ** CAN CHANGE THIS **
#define eepRefresh 2000    // EEPROM Refresh in ms
#define wifiDisable 60000  // turn off WiFi in ms

extern bool testSpeedo = false;      // for testing only, vary final pwmFrequency for speed ** CAN CHANGE THIS **
extern bool testCal = false;         // for testing only, vary final pwmFrequency for speed ** CAN CHANGE THIS **
extern bool hasNeedleSweep = false;  // for needle sweep ** CAN CHANGE THIS **
extern uint8_t sweepSpeed = 18;      // for needle sweep rate of change (in ms) ** CAN CHANGE THIS **

#define averageFilter 6                  // number of samples to take to average/remove erraticness from freq. changes.  Higher number, more samples ** CAN CHANGE THIS **
#define durationReset 1500               // duration of 'last sample' before reset speed back to zero
uint16_t motorPerformance[385];          // for copying the motorPerformance data on selection of calibration value
extern uint8_t motorPerformanceVal = 0;  // stored EEP value for calibration
extern bool updateMotorPerformance = false;

extern uint16_t maxFreqHall = 200;  // max frequency for top speed using the 02J / 02M hall sensor ** CAN CHANGE THIS IN WiFI **
extern uint16_t maxSpeed = 200;     // minimum cluster speed in kmh on the cluster ** CAN CHANGE THIS IN WiFI **

extern uint8_t speedOffset = 0;  // for adjusting a GLOBAL FIXED speed offset - so the entire range is offset by X value.  Might be easier to use this than the input max freq.
#define speedMultiplier 1
extern bool speedOffsetPositive = true;  // set to 1 for the above value to be ADDED, set to zero for the above value to be SUBTRACTED

#define convertToMPH 0  // for speedos that ONLY read in mph (and therefore the calibration is in mph), change KMH to find MPH
#define mphFactor 0.621371

#define pinMotorOutput 2  // pin for motor PWM output - needs stepped up to 5v for the motor (NPN transistor on the board).  Needs to support LED PWM(!)
#define pinSpeedInput 5   // interrupt supporting pin for speed input.  ESP32 C3 doesn't like them all, so if changing test this first(!)
#define pinDirection 10   // motor direction pin (currently unused) but here for future revisions
#define pinOnboardLED 8   // for feedback for input checking / flash LED on input.  ESP32 C3 is Pin 8

#define wifiHostName "SpeedPulser"  // the WiFi name

// if serialDebug is on, allow Serial talkback
#ifdef serialDebug
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(x...) Serial.printf(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x...)
#endif

extern unsigned long dutyCycleIncoming = 0;  // Duty Cycle % coming in from Can2Cluster or Hall
extern long tempSpeed = 0;                   // for testing only, set fixed speed in kmh.  Can set to 0 to speed up / slow down on repeat with testSpeed enabled
extern long tempDutyCycle = 0;
extern bool testNeedleSweep = false;  // set in WiFi, if testing needle sweep, this will be set
extern long pwmFrequency = 10000;     // PWM Hz (motor supplied is 10kHz)
extern long dutyCycle = 0;            // starting / default Hz: 0% is motor 'off'
extern int pwmResolution = 10;        // number of bits for motor resolution.  Can use 8 or 10, although 8 makes it a bit 'jumpy'

extern int rawCount = 0;  // counter for pulses incoming
extern unsigned long lastPulse = 0;
extern uint8_t incomingFreqMedian[] = { 0, 0, 0, 0, 0 };

extern bool ledOnboard = false;
extern int ledCounter = 0;

// WiFi Calls
extern void connectWifi();
extern void disconnectWifi();
extern void setupUI();
extern void textCallback(Control *sender, int type);
extern void generalCallback(Control *sender, int type);
extern void updateCallback(Control *sender, int type);
extern void getTimeCallback(Control *sender, int type);
extern void graphAddCallback(Control *sender, int type);
extern void graphClearCallback(Control *sender, int type);
extern void randomString(char *buf, int len);
extern void extendedCallback(Control *sender, int type, void *param);
extern void updateLabels();
extern void setupOTA();

// EEPROM Calls
extern void readEEP();
extern void writeEEP();

// Update Motor Performance Array
extern void updateMotorArray();

// UI handles - for WiFi
uint16_t bool_NeedleSweep, int16_sweepSpeed;
uint16_t bool_testSpeedo, int16_tempSpeed, bool_testCal;

uint16_t bool_positiveOffset, int16_speedOffset;
uint16_t int16_minSpeed, int16_maxSpeed, int16_minHall, int16_maxHall, int16_minCAN, int16_maxCAN, int16_calNumber;
int label_speed, label_currentPWM;

uint16_t graph;
uint16_t mainTime;
volatile bool updates = false;