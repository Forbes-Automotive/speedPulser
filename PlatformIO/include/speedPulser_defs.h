#ifndef SPEED_PULSER_DEFS_H
#define SPEED_PULSER_DEFS_H

#include <Arduino.h>
#include <driver/ledc.h>   // LEDC hardware PWM
#include <Preferences.h>   // NVS settings store
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>      // web UI files

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
//     serial debug statement (a build flag -D enableDebug=1 overrides it).
//   * With enableDebug = 1, toggle the per-subsystem flags below to pick which
//     tagged Serial streams you want. Each prints one line with a [TAG] prefix.
// ---------------------------------------------------------------------------
#ifndef enableDebug
#define enableDebug 0 // ** Main ** 0 = silence ALL serial debug
#endif

#define debugSys 1   // [SYS]  boot / general
#define debugPower 1 // [PWR]  power_manager (reduced power / wake)
#define debugWifi 1  // [WiFi] soft-AP + web server bring-up
#define debugIO 1    // [IO]   GPIO / LEDC PWM / interrupt init
#define debugEEP 1   // [EEP]  settings load & save
#define debugCtrl 1  // [CTRL] speed input / needle target
#define debugFB 1    // [FB]   motor feedback / drive trim / calibration
#define debugWeb 1   // [WEB]  web server / API / UI / OTA

#ifndef baudSerial
#define baudSerial 115200 // serial monitor baud rate
#endif

#if enableDebug && debugSys
#define DEBUG(x, ...) Serial.printf("[SYS] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG(x, ...)
#endif
#if enableDebug && debugWifi
#define DEBUG_WIFI(x, ...) Serial.printf("[WiFi] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_WIFI(x, ...)
#endif
#if enableDebug && debugIO
#define DEBUG_IO(x, ...) Serial.printf("[IO] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_IO(x, ...)
#endif
#if enableDebug && debugEEP
#define DEBUG_EEP(x, ...) Serial.printf("[EEP] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_EEP(x, ...)
#endif
#if enableDebug && debugCtrl
#define DEBUG_CTRL(x, ...) Serial.printf("[CTRL] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_CTRL(x, ...)
#endif
#if enableDebug && debugFB
#define DEBUG_FB(x, ...) Serial.printf("[FB] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_FB(x, ...)
#endif
#if enableDebug && debugWeb
#define DEBUG_WEB(x, ...) Serial.printf("[WEB] " x "\n", ##__VA_ARGS__)
#else
#define DEBUG_WEB(x, ...)
#endif

#define SETTINGS_SAVE_INTERVAL_MS 2000 // the settings task saves every 2 s (unchanged values cost nothing)

// ===== Pins (ESP32-C3) =====
#define pinMotorOutput 2 // motor PWM output (stepped up to 5 V by an NPN on the board)
#define pinSpeedInput 5  // speed sensor input (opto-isolated). Not every C3 pin takes an interrupt well: test first if changing
#define pinDirection 10  // motor direction: LOW = normal, HIGH = reversed
#define pinFeedback 4    // motor tacho input (voltage board; the C3 has no pulse counter, so an interrupt times it)
#define pinOnboardLED 8  // flashes with the speed input

// Voltage board (detected at boot), against PCBs/rev6-allConfirmed (SpeedPulserV5, ESP32-C3 SuperMini U9):
//   GPIO1 V_MOT_SENSE, GPIO2 ESP_MOTOR, GPIO3 R22 1k strap, GPIO4 ESP_MFB, GPIO5 ESP_SPEEDPULSE,
//   GPIO6 V_ADJ, GPIO7 EN_ADJ (R7 10k pull-down), GPIO10 ESP_MOTOR_DIR; GPIO0/8/9/20/21 unused.
#define pinBoardVersion 3  // R22 1k to GND on the voltage board -> reads LOW; a legacy board floats HIGH (INPUT_PULLUP)
#define pinVoltageAdjust 6 // V_ADJ: PWM "DAC" into the TPS62933 FB node (INVERSE: higher duty -> lower motor volts)
#define pinBuckEnable 7    // buck EN: HIGH enables the adjustable motor supply (LOW = off at boot)
#define pinMotorSense 1    // V_MOT_SENSE: motor rail via R24 39k / R25 10k (+C33 100 nF) -> ADC1_CH1

#define wifiHostName "SpeedPulser" // the WiFi name

// ===== Speed sensor input =====
// The sensor is described by one number: pulses per km. VW/Audi gearbox sensors
// give 1 Hz per km/h = 3600 pulses per km; 4000 / 8000 / 16000 pulses-per-mile
// sensors are 2485.5 / 4971 / 9942 pulses per km. Speed (km/h) = Hz x 3600 / pulses per km.
#define SENSOR_PULSES_PER_KM_DEFAULT 3600.0f
#define SENSOR_PULSES_PER_KM_MIN 100.0f
#define SENSOR_PULSES_PER_KM_MAX 50000.0f
#define INPUT_SMOOTHING_MS_DEFAULT 200 // the speed is averaged over at least this long (more pulses = smoother)
#define INPUT_SMOOTHING_MS_MIN 50
#define INPUT_SMOOTHING_MS_MAX 1000
#define INPUT_TIMEOUT_MS 1500          // no sensor pulse for this long = stopped (needle to zero)...
#define INPUT_TIMEOUT_MAX_MS 5000      // ...longer for a slow sensor (V5.09), up to this:
#define INPUT_TIMEOUT_SLOW_KPH 10.0f   //    INPUT_TIMEOUT_SLOW_PERIODS pulse periods at this speed
#define INPUT_TIMEOUT_SLOW_PERIODS 2.5f
#define KPH_TO_MPH 0.621371f

// ===== Needle sweep (speedPulser_io.cpp) =====
#define SWEEP_TICK_MS 10           // output update rate while sweeping
#define SWEEP_EASE 0.2f            // last fraction of the climb spent slowing to a stop at the top (0..1)
#define SWEEP_TOP_HOLD_MS 400      // pause at the top before coming back (no tacho)
#define SWEEP_TOP_WAIT_MAX_MS 1500 // with a tacho: wait at the top until the motor is there, but no longer than this
#define SWEEP_TOP_ARRIVED 0.97f    // "there" = the tacho is within 3 % of the top mark's Hz
#define SWEEP_TOP_DWELL_MS 150     // then rest on the mark this long

// ===== Tuning (V5.09) =====
// Every value here was set on the bench for the TY3816B motor and a VW / Audi speedo;
// change them for different hardware. Each one is explained where it is used.
// Elsewhere: rail and drive limits in speedPulser_voltage.h, the drive table size in
// speedPulser_driveCal.h.

// Speed input glitch filter: half a period at Maximum speed, kept within these
#define INPUT_GLITCH_MIN_US 200        // 2.5 kHz: nothing real is faster
#define INPUT_GLITCH_MAX_US 3000       // the V5.05 fixed limit: no slower sensor needs more

// Motor tacho + feedback trim (speedPulser_control.cpp)
#define FB_PULSES_PER_REV 5       // TY3816B tacho: the interval pattern repeats every 5 pulses
constexpr uint32_t FB_GLITCH_US = 1000;     // tacho edges closer than this are line noise (top speed ~254 Hz = 3.9 ms)
constexpr uint32_t FB_STALL_US = 600000UL; // no edge for 0.6 s: stopped (the motor's floor is ~6 Hz = 0.17 s)
constexpr uint32_t FB_SPAN_US = 200000UL;  // average over at least this much time (whole turns)
constexpr float FB_MEAS_ALPHA = 0.5f;      // EMA weight for the measured-Hz low-pass
constexpr float FB_REF_TAU_S = 0.35f;     // the motor + measurement follow a step with this time constant (bench)
constexpr float FB_REF_SETTLED = 0.05f; // integrate once the model is within 5 % of the target
constexpr uint8_t FB_LOST_TICKS = 15;      // 1.5 s of control ticks (a start from rest reads 0 for up to ~1 s)
constexpr float FB_LOST_MIN_HZ = 9.0f;     // below this a real motor can read 0 (its floor)
constexpr float FB_PLAUSIBLE = 0.5f;       // integrate only within ±50 % of the model
constexpr float CAL_DRIVE_MAX_STEP_V = 0.5f; // one Calibration Mode drive request may raise the drive by this much

// Auto Fill (speedPulser_autoFill.cpp)
constexpr uint16_t AF_LOW_GRID[] = {10, 20, 30, 40}; // the motor bends most down here
constexpr uint16_t AF_STEPS[] = {10, 15, 20, 25, 30, 40, 50}; // spacing above 40, first that fits
constexpr uint16_t AF_MIN_GAP = 5;     // no auto mark this close to an eye mark
constexpr float AF_MIN_HZ = 9.0f;      // motor floor is ~6-8 Hz at a 4.8 V rail: keep auto marks clear of it
constexpr float AF_SKIP_HZ = 25.0f;    // an auto mark below this that can't be held is left out
constexpr float AF_TOL_HZ = 0.15f;     // done when within max(this, AF_TOL_FRAC x target)
constexpr float AF_TOL_FRAC = 0.005f;
constexpr float AF_ACCEPT_FRAC = 0.02f; // after AF_MAX_ITERS the best try must be this close
constexpr uint8_t AF_MAX_ITERS = 8;
constexpr uint8_t AF_MAX_STALLS = 6;
constexpr float AF_SLOPE0 = 36.0f;     // Hz per V of drive (TY3816B at a 4.8 V rail), refined as it goes
constexpr float AF_SLOPE_MIN = 8.0f, AF_SLOPE_MAX = 150.0f;
constexpr float AF_STEP_FRAC = 0.25f;  // largest drive change per try (fraction of the drive)
constexpr float AF_STEP_MIN_V = 0.05f;
constexpr uint16_t AF_SETTLE_START = 25; // ticks (100 ms): first mark, from rest
constexpr uint16_t AF_SETTLE_LOW = 16;   // after a change, below 25 Hz (slower measurement)
constexpr uint16_t AF_SETTLE = 12;
constexpr uint16_t AF_HOLD_LOW = 20;     // averaging, below 25 Hz
constexpr uint16_t AF_HOLD = 10;

// ===== Settings (persisted) =====
extern bool needleSweepOnBoot;           // run the needle sweep at power-up
extern uint8_t needleSweepSpeed;         // needle sweep pace (1..50, ms per dial km/h or mph)
extern uint8_t presetId;                 // legacy board: selected calibration preset (1..n, or CUSTOM_CAL_ID)
extern uint16_t presetSpeedTable[385];   // legacy board: the selected preset, speed for each 10-bit duty
extern float sensorPulsesPerKm;          // speed sensor pulses per km (see above)
extern uint16_t inputSmoothingMs;        // speed input averaging time (ms)
extern uint16_t maxSpeed;                // highest speed shown (km/h input); faster input holds here
extern int16_t globalSpeedOffsetKph;     // fixed offset added to every speed (km/h, + or -)
extern bool clusterInMph;                // the dial reads mph: km/h input is converted before the motor
extern bool useSpeedOffsetCurve;         // use the 5-point offset curve instead of the global offset
#define SPEED_OFFSET_CURVE_POINTS 5
extern int16_t speedOffsetCurveOffsets[SPEED_OFFSET_CURVE_POINTS]; // offset (km/h) at 25 / 75 / 125 / 175 / 225 km/h
extern bool reverseDirection;            // motor direction: false = normal (pinDirection LOW)
extern bool feedbackEnable;              // voltage board: hold the calibrated tacho speed with the drive trim
extern float feedbackDeadband;           // voltage board: trim ignores errors smaller than this (Hz)
extern uint16_t feedbackMinSpeed;        // voltage board: no trim below this needle speed (cluster units)

// ===== Live state =====
extern bool speedTestOn;                 // Speed Test: drive the needle to speedTestKph instead of the sensor
extern uint16_t speedTestKph;            // Speed Test speed (km/h, converted like the sensor input)
extern bool calibrationModeOn;           // Calibration Mode: the user (or Auto Fill) sets the motor directly
extern bool needleSweepRequested;        // the UI asked for a needle sweep (run by the control task)
extern uint16_t measuredSpeed;           // voltage board: motor tacho speed as a dial reading (cluster units)
extern bool feedbackAvailable;           // a real tacho signal has been seen this session
extern bool feedbackMissing;             // the motor runs but no tacho signal (voltage board only)

extern Preferences settingsStore;        // NVS: every setting lives in the namespace "hasNeedleSweep" (see readEEP)

// ===== Functions =====
void basicInit();                        // GPIO, LEDC PWM, interrupts (io.cpp)
void applyDirection();                   // drive pinDirection from reverseDirection (io.cpp)
void setMotorDutyRaw(uint32_t pwmDuty);  // write a 12-bit hardware duty (io.cpp)
void needleSweep();                      // sweep the needle up and back (io.cpp)
void setupWebServer();                   // speedPulser_webserver.cpp
void connectWifi();                      // speedPulser_wifi.cpp
void readEEP();                          // load settings (speedPulser_eep.cpp)
void writeEEP();                         // save settings (speedPulser_eep.cpp)
void updateMotorArray();                 // copy the selected preset into presetSpeedTable (motorCal.cpp)
const char *getCalibrationText(uint8_t calibrationVal);
uint8_t getCalibrationCount();
void taskInit();                         // start the FreeRTOS tasks (tasks.cpp)

#endif // SPEED_PULSER_DEFS_H
