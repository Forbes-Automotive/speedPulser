#ifndef SPEED_PULSER_DRIVECAL_H
#define SPEED_PULSER_DRIVECAL_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SpeedPulser — drive calibration (voltage board, V5.01)
//
// A short table of needle marks captured in Calibration Mode: for each mark the
// user reads off the dial, the drive (V, see speedPulser_voltage.h) that put the
// needle there and the motor tacho Hz it produced. Speed is in cluster units, the
// way the user reads the dial.
//
// Running is open-loop from this table: target speed -> interpolated drive. The
// same table gives the tacho Hz expected at a speed (for the optional feedback
// trim) and the reverse, the speed a measured Hz means (the Measured Speed
// read-out). Real dials aren't linear near zero, so the more marks captured —
// especially one low on the dial — the closer both are.
//
// Between marks: straight lines. Below the lowest mark the first segment is
// extended down (a motor needs some drive before it turns at all); above the top
// mark the speed is capped at the top mark: driving past the highest captured
// mark would extrapolate into speeds nobody proved and overspin the motor. With a
// single mark the line runs through zero (and that mark is also the cap).
// ---------------------------------------------------------------------------

#define DRIVE_CAL_MAX 10

struct DriveCalPoint
{
    uint16_t speed; // dial reading (cluster units)
    float hz;       // tacho Hz at that reading (0 = no tacho fitted when captured)
    float drive;    // drive (V) that produced it
    float rail;     // rail (V) at capture (info)
    float pwm;      // PWM fraction at capture (info)
};

extern DriveCalPoint driveCal[DRIVE_CAL_MAX]; // sorted by speed, ascending
extern uint8_t driveCalCount;

// driveCalAdd() result codes
#define DCAL_OK             0
#define DCAL_ERR_NO_MOTION  1 // tacho fitted but the motor isn't turning
#define DCAL_ERR_UNSTABLE   2 // tacho still settling — wait and capture again
#define DCAL_ERR_ORDER      3 // a higher mark needs more drive / more Hz than its neighbours
#define DCAL_ERR_FULL       4 // table full — delete a mark first
#define DCAL_ERR_SPEED      5 // speed out of range

uint8_t driveCalCapture(uint16_t speed); // capture the live drive + tacho as the mark `speed`
void driveCalDelete(uint8_t index);
void driveCalClear();
bool driveCalHzValid();                  // every mark has a tacho reading (feedback + measured speed usable)
uint16_t driveCalTopSpeed();             // highest captured mark (the drive speed cap); 0 = no table

float driveForSpeed(uint16_t speed);     // calibrated drive (V) for a dial speed; 0 at speed 0
float hzForSpeed(float speed);           // tacho Hz expected at a dial speed
float speedForHz(float hz);              // dial speed a tacho Hz means

const char *driveCalErrorText(uint8_t code);

// Export / import as text. The file carries "type": DRIVE_CAL_FILE_TYPE so it is
// never confused with a legacy PWM-duty cal ("type": PWM_CAL_FILE_TYPE).
#define DRIVE_CAL_FILE_TYPE "speedpulser-voltage-cal"
void driveCalExportJson(String &out);
bool driveCalImportJson(const char *json, const char *&err); // false + err if unusable (table untouched)

#endif // SPEED_PULSER_DRIVECAL_H
