#include "speedPulser_defs.h"

// Global settings and live state shared between the tasks. Declared (with what
// each one means) in speedPulser_defs.h.

AsyncWebServer server(80);
Preferences settingsStore;

// ===== Settings (defaults; readEEP() loads the saved values) =====
bool needleSweepOnBoot = false;
uint8_t needleSweepSpeed = 18;
uint8_t presetId = 1;
uint16_t presetSpeedTable[385] = {0};
float sensorPulsesPerKm = SENSOR_PULSES_PER_KM_DEFAULT;
uint16_t inputSmoothingMs = INPUT_SMOOTHING_MS_DEFAULT;
uint16_t maxSpeed = 200;
int16_t globalSpeedOffsetKph = 0;
bool clusterInMph = false;
bool useSpeedOffsetCurve = false;
int16_t speedOffsetCurveOffsets[SPEED_OFFSET_CURVE_POINTS] = {0, 0, 0, 0, 0};
bool reverseDirection = false;
bool feedbackEnable = true;      // voltage board defaults (SpeedPulser V5.04 bench tuning)
float feedbackDeadband = 0.3f;
uint16_t feedbackMinSpeed = 8;

// ===== Live state =====
bool speedTestOn = false;
uint16_t speedTestKph = 0;
bool calibrationModeOn = false;
bool needleSweepRequested = false;
uint16_t measuredSpeed = 0;
bool feedbackAvailable = false;
bool feedbackMissing = false;
