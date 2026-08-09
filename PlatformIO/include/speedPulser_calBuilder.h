#ifndef SPEED_PULSER_CAL_BUILDER_H
#define SPEED_PULSER_CAL_BUILDER_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SpeedPulserV2 — User Calibration Builder
//
// Lets a user author their own speedometer calibration by capturing a handful
// of "anchor" points — each a (speed, duty) pair measured live on the gauge —
// and expanding them into the full 385-entry motorPerformance[] table used by
// the runtime.  Anchors are stored compactly in NVS and the table is rebuilt
// on boot, so a custom calibration survives power cycles.
//
// The captured duty is at full 12-bit hardware resolution (0..PWM_DUTY_MAX).
// The generated table stays in the legacy 10-bit index domain (385 entries,
// one per duty = i << DUTY_SCALE_SHIFT) so it drops straight into the existing
// preset machinery and can be exported as a ready-to-share preset array.
// ---------------------------------------------------------------------------

#define CUSTOM_CAL_ID     200   // motorPerformanceVal value that selects the custom cal
#define MAX_CAL_POINTS     32   // hard cap on captured anchors (~128 bytes NVS)
#define CAL_NAME_MAX       40   // max chars for the calibration name

struct CalPoint {
  uint16_t speed;  // gauge reading in the cluster's native unit (kmh or mph)
  uint16_t duty;   // 12-bit hardware duty that produced that reading
};

// In-RAM working set (defined in speedPulser_calBuilder.cpp)
extern CalPoint customCalPoints[MAX_CAL_POINTS];
extern uint8_t  customCalCount;
extern char     customCalName[CAL_NAME_MAX];
extern bool     customCalUnitMph;   // metadata: was cluster in MPH when captured
extern bool     customCalValid;     // true once >=2 points and a table is built

// Lifecycle / persistence
void calBuilderInit();          // load anchors from NVS, rebuild table (call before updateMotorArray)
void calSaveToNvs();            // persist current anchors + name to NVS

// Anchor editing (each rebuilds the table on success)
bool calAddPoint(uint16_t speed, uint16_t duty);  // insert/replace, kept sorted by duty
bool calDeletePoint(uint8_t index);
void calClearPoints();
void calSetName(const char *name);

// Table generation
void buildCustomCalTable();     // fill motorPerformance[] from the current anchors

// Runtime lookup — interpolate a requested speed straight to a full-resolution
// 12-bit duty from the anchor points, bypassing the 385-entry / 10-bit table
// (which can only represent duty up to 384<<DUTY_SCALE_SHIFT). Returns 0 below
// the first anchor's speed (dead-zone) and holds the last anchor's duty above it.
uint32_t customSpeedToDuty12(uint16_t speed);

// Export / import as text
void calExportJson(String &out);    // shareable round-trippable JSON
void calExportCArray(String &out);  // drop-in motorPerformanceN[] C source block
bool calImportJson(const char *json);  // parse anchors from exported JSON

#endif  // SPEED_PULSER_CAL_BUILDER_H
