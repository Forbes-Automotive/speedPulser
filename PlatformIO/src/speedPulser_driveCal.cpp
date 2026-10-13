#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"
#include "speedPulser_ver.h"

// See speedPulser_driveCal.h for the design overview.

DriveCalPoint driveCal[DRIVE_CAL_MAX];
uint8_t driveCalCount = 0;

// Piecewise-linear y(v) through n points with ascending x. The first and last
// segments extend past the ends; one point is a line through zero. Never < 0.
static float interp(const float *x, const float *y, uint8_t n, float v)
{
    if (n == 0)
        return 0.0f;
    if (n == 1)
        return (x[0] > 0.0f) ? y[0] * v / x[0] : 0.0f;
    uint8_t i = 1;
    while (i < n - 1 && v > x[i])
        i++;
    const float dx = x[i] - x[i - 1];
    const float r = (dx > 0.0f) ? y[i - 1] + (v - x[i - 1]) * (y[i] - y[i - 1]) / dx : y[i];
    return r < 0.0f ? 0.0f : r;
}

bool driveCalHzValid()
{
    if (driveCalCount == 0)
        return false;
    for (uint8_t i = 0; i < driveCalCount; i++)
        if (driveCal[i].hz <= 0.0f)
            return false;
    return true;
}

uint16_t driveCalTopSpeed()
{
    return driveCalCount > 0 ? driveCal[driveCalCount - 1].speed : 0;
}

float driveForSpeed(uint16_t speed)
{
    if (speed == 0)
        return 0.0f;
    const uint16_t cap = driveCalTopSpeed();
    if (cap > 0 && speed > cap)
        speed = cap; // never extrapolate the drive past the highest proven mark
    float d;
    if (driveCalCount == 0)
    {
        // Uncalibrated: a straight guess up to Max Speed at Nominal PWM, just so
        // Speed Test does something before the first capture.
        const uint16_t top = maxSpeed > 0 ? maxSpeed : 200;
        d = (float)speed / (float)top * driveMaxV() * vcPwmNominal;
    }
    else
    {
        float x[DRIVE_CAL_MAX], y[DRIVE_CAL_MAX];
        for (uint8_t i = 0; i < driveCalCount; i++)
        {
            x[i] = driveCal[i].speed;
            y[i] = driveCal[i].drive;
        }
        d = interp(x, y, driveCalCount, (float)speed);
    }
    const float hi = driveMaxV();
    return d > hi ? hi : d;
}

float hzForSpeed(float speed)
{
    if (speed <= 0.0f || !driveCalHzValid())
        return 0.0f;
    float x[DRIVE_CAL_MAX], y[DRIVE_CAL_MAX];
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        x[i] = driveCal[i].speed;
        y[i] = driveCal[i].hz;
    }
    return interp(x, y, driveCalCount, speed);
}

float speedForHz(float hz)
{
    if (hz <= 0.0f || !driveCalHzValid())
        return 0.0f;
    float x[DRIVE_CAL_MAX], y[DRIVE_CAL_MAX];
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        x[i] = driveCal[i].hz;
        y[i] = driveCal[i].speed;
    }
    return interp(x, y, driveCalCount, hz);
}

// The table changed: keep everything that derives from it in step.
static void driveCalChanged()
{
    topSpeedCaptured = (driveCalCount > 0);
    syncFeedbackMaxFreq();
    resetDriveControl();
}

uint8_t driveCalCapture(uint16_t speed)
{
    if (speed < 1 || speed > 400)
        return DCAL_ERR_SPEED;

    DriveCalPoint p;
    p.speed = speed;
    p.drive = lastDriveV;
    p.rail = lastRailCmdV;
    p.pwm = lastPwmFrac;
    p.hz = 0.0f;
    if (feedbackAvailable)
    {
        // Tacho fitted: the motor must be turning steadily, averaged over ~1 s.
        float meanHz, spreadPct, driftHz;
        if (!feedbackHzWindow(meanHz, spreadPct, driftHz))
            return DCAL_ERR_NO_MOTION;
        if (!feedbackHzSteady(meanHz, spreadPct, driftHz))
            return DCAL_ERR_UNSTABLE;
        p.hz = meanHz;
    }
    if (p.drive <= 0.0f)
        return DCAL_ERR_NO_MOTION;

    // Same mark captured again: replace it.
    int8_t same = -1;
    for (uint8_t i = 0; i < driveCalCount; i++)
        if (driveCal[i].speed == speed)
            same = (int8_t)i;
    if (same < 0 && driveCalCount >= DRIVE_CAL_MAX)
        return DCAL_ERR_FULL;

    // A higher mark must need more drive (and give more Hz) than every lower one.
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        if ((int8_t)i == same)
            continue;
        const DriveCalPoint &q = driveCal[i];
        const bool below = q.speed < speed;
        if (below ? (q.drive >= p.drive) : (q.drive <= p.drive))
            return DCAL_ERR_ORDER;
        if (q.hz > 0.0f && p.hz > 0.0f && (below ? (q.hz >= p.hz) : (q.hz <= p.hz)))
            return DCAL_ERR_ORDER;
    }

    if (same >= 0)
    {
        driveCal[same] = p;
    }
    else
    {
        uint8_t pos = driveCalCount;
        while (pos > 0 && driveCal[pos - 1].speed > speed)
        {
            driveCal[pos] = driveCal[pos - 1];
            pos--;
        }
        driveCal[pos] = p;
        driveCalCount++;
    }
    DEBUG_FB("cal mark %u = %.2f Hz @ drive %.2f V (rail %.2f V x %.0f%% PWM), %u marks",
             p.speed, p.hz, p.drive, p.rail, p.pwm * 100.0f, driveCalCount);
    driveCalChanged();
    return DCAL_OK;
}

void driveCalDelete(uint8_t index)
{
    if (index >= driveCalCount)
        return;
    for (uint8_t i = index; i + 1 < driveCalCount; i++)
        driveCal[i] = driveCal[i + 1];
    driveCalCount--;
    driveCalChanged();
}

void driveCalClear()
{
    driveCalCount = 0;
    driveCalChanged();
}

// ===== Export / import =====
// Tagged with "type" so a voltage cal file can't be mistaken for (or imported
// as) a legacy PWM-duty cal. The drive settings the marks were captured with go
// along too: the same drive splits into a different rail x PWM if they change.
void driveCalExportJson(String &out)
{
    JsonDocument doc;
    doc["type"] = DRIVE_CAL_FILE_TYPE;
    doc["about"] = "Voltage calibration (voltage board, Voltage Control on): needle marks vs "
                   "drive in volts (rail x PWM). Not a PWM-duty preset.";
    doc["firmware"] = VERSION;
    doc["unit"] = convertToMPH ? "mph" : "kmh";
    doc["pwmNominal"] = vcPwmNominal;
    doc["railMinV"] = RAIL_V_FLOOR;
    doc["railMaxV"] = vcRailMaxV;
    JsonArray marks = doc["marks"].to<JsonArray>();
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        JsonObject m = marks.add<JsonObject>();
        m["speed"] = driveCal[i].speed;
        m["hz"] = driveCal[i].hz;
        m["drive"] = driveCal[i].drive;
        m["rail"] = driveCal[i].rail;
        m["pwm"] = driveCal[i].pwm;
    }
    serializeJsonPretty(doc, out);
}

bool driveCalImportJson(const char *json, const char *&err)
{
    err = "Not a voltage calibration file";
    JsonDocument doc;
    if (!json || deserializeJson(doc, json))
        return false;
    if (strcmp(doc["type"] | "", DRIVE_CAL_FILE_TYPE) != 0)
        return false;
    JsonArray marks = doc["marks"].as<JsonArray>();
    if (marks.isNull() || marks.size() == 0)
    {
        err = "The file has no marks";
        return false;
    }
    if (marks.size() > DRIVE_CAL_MAX)
    {
        err = "Too many marks in the file";
        return false;
    }

    // Build the table aside, sorted by speed, and only take it if it is usable:
    // the same ordering rule a capture has to pass.
    DriveCalPoint tmp[DRIVE_CAL_MAX];
    uint8_t n = 0;
    for (JsonObject m : marks)
    {
        DriveCalPoint p;
        p.speed = m["speed"] | 0;
        p.hz = m["hz"] | 0.0f;
        p.drive = m["drive"] | 0.0f;
        p.rail = m["rail"] | 0.0f;
        p.pwm = m["pwm"] | 0.0f;
        if (p.speed < 1 || p.speed > 400 || p.drive <= 0.0f)
        {
            err = "A mark in the file has no speed or drive";
            return false;
        }
        uint8_t pos = n;
        while (pos > 0 && tmp[pos - 1].speed > p.speed)
        {
            tmp[pos] = tmp[pos - 1];
            pos--;
        }
        tmp[pos] = p;
        n++;
    }
    for (uint8_t i = 1; i < n; i++)
    {
        const DriveCalPoint &a = tmp[i - 1], &b = tmp[i];
        if (a.speed == b.speed || a.drive >= b.drive || (a.hz > 0.0f && b.hz > 0.0f && a.hz >= b.hz))
        {
            err = "The marks in the file are out of order";
            return false;
        }
    }

    vcPwmNominal = constrain(doc["pwmNominal"] | vcPwmNominal, 0.2f, 1.0f);
    vcRailMaxV = constrain(doc["railMaxV"] | vcRailMaxV, RAIL_V_FLOOR, RAIL_V_CEIL);
    convertToMPH = (strcmp(doc["unit"] | (convertToMPH ? "mph" : "kmh"), "mph") == 0);
    memcpy(driveCal, tmp, sizeof(DriveCalPoint) * n);
    driveCalCount = n;
    driveCalChanged();
    DEBUG_FB("voltage cal imported: %u marks, %u-%u (nominal PWM %.0f%%, max rail %.1f V)",
             n, tmp[0].speed, tmp[n - 1].speed, vcPwmNominal * 100.0f, vcRailMaxV);
    err = "OK";
    return true;
}

const char *driveCalErrorText(uint8_t code)
{
    switch (code)
    {
    case DCAL_ERR_NO_MOTION: return "Motor not turning: raise the drive until the needle moves";
    case DCAL_ERR_UNSTABLE:  return "Motor speed still settling: wait a second and capture again";
    case DCAL_ERR_ORDER:     return "Out of order: a higher mark needs more drive than the marks below it. Check the reading, or delete the old mark";
    case DCAL_ERR_FULL:      return "All marks used: delete one first";
    case DCAL_ERR_SPEED:     return "Speed must be 1-400";
    default:                 return "OK";
    }
}
