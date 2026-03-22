#include "speedPulser_defs.h"

/*
Global variable definitions for SpeedPulser project.
These are the actual definitions (not just declarations) of all global variables.
Headers should only include extern declarations.
*/

// ===== Web Server Instance =====
AsyncWebServer server(80);

// ===== EEPROM/Preferences =====
Preferences pref;

// ===== Testing and Configuration Variables =====
bool testSpeedo = false;               // for testing only, vary final pwmFrequency for speed
bool testCal = false;                  // for testing only, vary final pwmFrequency for speed
bool hasNeedleSweep = false;           // for needle sweep
uint8_t sweepSpeed = 18;               // for needle sweep rate of change (in ms)

// ===== Motor Performance and Calibration =====
uint16_t motorPerformance[385] = {0};  // for copying the motorPerformance data on selection of calibration value
uint8_t motorPerformanceVal = 0;       // stored EEP value for calibration
bool updateMotorPerformance = false;   // flag to update motor array

// ===== Speed Control Configuration =====
uint16_t maxFreqHall = 200;           // max frequency for top speed using Hall sensor
uint16_t maxSpeed = 200;              // minimum cluster speed in kmh on the cluster
uint8_t speedOffset = 0;              // for adjusting a GLOBAL FIXED speed offset
bool speedOffsetPositive = true;      // set to 1 for offset to be ADDED, 0 for SUBTRACTED
bool convertToMPH = false;            // convert km/h input to mph before lookup
bool useSpeedOffsetCurve = false;     // apply 5-point fixed-range offset curve instead of global offset
int16_t speedOffsetCurveOffsets[SPEED_OFFSET_CURVE_POINTS] = {0, 0, 0, 0, 0};

// ===== Test Speed Variable =====
uint16_t tempSpeed = 0;               // for testing only, set fixed speed in kmh

// ===== Signal Filter =====
uint8_t averageFilter = 6;            // number of samples for median filter (1-10)

#if serialDebug
bool serialActive = false;  // set true in setup() if a USB Serial host is detected in time
#endif
