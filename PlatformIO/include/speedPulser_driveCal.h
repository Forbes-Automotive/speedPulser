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
//
// V5.05 Auto Fill: each mark is either an EYE mark (the user put the needle on it
// and captured it) or an AUTO mark (Auto Fill placed it). Only the eye marks need
// a person: they fix where the needle points for a tacho Hz (the dial). Auto Fill
// keeps every eye mark's speed and Hz, re-measures its drive with the tacho, and
// adds auto marks in between and below on the dial line through the eye marks
// (speedPulser_autoFill.cpp). The table it replaces is kept as one backup.
// ---------------------------------------------------------------------------

#define DRIVE_CAL_MAX 20 // V5.05: was 10. NVS blob stays 20 bytes per mark

#define DCAL_SRC_EYE 0  // captured by the user, needle on the mark
#define DCAL_SRC_AUTO 1 // placed and measured by Auto Fill

struct DriveCalPoint
{
    uint16_t speed; // dial reading (cluster units)
    uint8_t src;    // DCAL_SRC_EYE / DCAL_SRC_AUTO (V5.05: was padding, see readEEP)
    uint8_t rsv;    // 0
    float hz;       // tacho Hz at that reading (0 = no tacho fitted when captured)
    float drive;    // drive (V) that produced it
    float rail;     // rail (V) at capture (info)
    float pwm;      // PWM fraction at capture (info)
};
static_assert(sizeof(DriveCalPoint) == 20, "DriveCalPoint is stored in NVS: keep its layout");

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
bool driveCalSetSource(uint8_t index, uint8_t src); // DCAL_SRC_EYE / DCAL_SRC_AUTO; false = no such mark
bool driveCalHzValid();                  // every mark has a tacho reading (feedback + measured speed usable)
uint16_t driveCalTopSpeed();             // highest captured mark (the drive speed cap); 0 = no table

float driveForSpeed(float speed);        // calibrated drive (V) for a dial speed; 0 at speed 0
float hzForSpeed(float speed);           // tacho Hz expected at a dial speed
float speedForHz(float hz);              // dial speed a tacho Hz means

const char *driveCalErrorText(uint8_t code);

// Piecewise-linear y(v) through n points with ascending x (see the .cpp).
float driveCalInterp(const float *x, const float *y, uint8_t n, float v);
// Replace the whole table (already sorted and checked), e.g. Auto Fill's result.
void driveCalReplace(const DriveCalPoint *pts, uint8_t n);
// A consistent copy of the table (another task may be changing it); returns the count.
uint8_t driveCalSnapshot(DriveCalPoint *out);

// One backup of a whole table (NVS, as an export file), taken just before Auto Fill
// replaces it. Restore swaps the two, so a restore can be undone the same way.
extern bool driveCalBackupValid;
bool driveCalSaveBackup();
bool driveCalRestoreBackup(const char *&err);

// ===== Auto Fill (speedPulser_autoFill.cpp) =====
#define AF_IDLE 0
#define AF_RUNNING 1
#define AF_DONE 2
#define AF_FAILED 3
extern volatile uint8_t autoFillState;
extern uint8_t autoFillIndex;   // mark being measured (0-based)
extern uint8_t autoFillTotal;   // marks in the plan
extern uint16_t autoFillSpeed;  // its dial speed
extern float autoFillTargetHz;  // its tacho Hz
extern char autoFillMsg[160];   // last result / error, for the UI
bool autoFillStart(const char *&err); // false + err if it can't start
void autoFillStop(const char *why);
void autoFillTick();                  // control task, every 100 ms in Calibration Mode
inline bool autoFillRunning() { return autoFillState == AF_RUNNING; }

// Export / import as text. The file carries "type": DRIVE_CAL_FILE_TYPE so it is
// never confused with a legacy PWM-duty cal ("type": PWM_CAL_FILE_TYPE).
#define DRIVE_CAL_FILE_TYPE "speedpulser-voltage-cal"
void driveCalExportJson(String &out, bool pretty = true);
bool driveCalImportJson(const char *json, const char *&err); // false + err if unusable (table untouched)

#endif // SPEED_PULSER_DRIVECAL_H
