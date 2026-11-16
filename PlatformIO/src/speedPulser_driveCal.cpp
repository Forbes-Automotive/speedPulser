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
float driveCalInterp(const float *x, const float *y, uint8_t n, float v)
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

float driveForSpeed(float speed)
{
    if (speed <= 0.0f)
        return 0.0f;
    const uint16_t cap = driveCalTopSpeed();
    if (cap > 0 && speed > (float)cap)
        speed = (float)cap; // never extrapolate the drive past the highest proven mark
    // No marks = no drive (V5.06). The old straight-line guess (Max Speed at Max
    // Rail x Nominal PWM, 9.6 V with the defaults) drove a new speedo far past the
    // top of its dial on the first Speed Test. Calibration Mode sets the drive
    // directly, so calibrating doesn't need this.
    if (driveCalCount == 0)
        return 0.0f;
    float x[DRIVE_CAL_MAX], y[DRIVE_CAL_MAX];
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        x[i] = driveCal[i].speed;
        y[i] = driveCal[i].drive;
    }
    const float d = driveCalInterp(x, y, driveCalCount, speed);
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
    return driveCalInterp(x, y, driveCalCount, speed);
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
    return driveCalInterp(x, y, driveCalCount, hz);
}

// The table changed: keep everything that derives from it in step.
static void driveCalChanged()
{
    resetDriveControl();
}

// Every change to the table is made under this lock, and the settings save copies
// it under the same lock (V5.09): the web task, the control task (Auto Fill) and
// the settings task all touch it, and a save could catch a half-moved table.
static portMUX_TYPE driveCalMux = portMUX_INITIALIZER_UNLOCKED;

static void driveCalStore(const DriveCalPoint *pts, uint8_t n)
{
    portENTER_CRITICAL(&driveCalMux);
    memcpy(driveCal, pts, sizeof(DriveCalPoint) * n);
    driveCalCount = n;
    portEXIT_CRITICAL(&driveCalMux);
}

uint8_t driveCalSnapshot(DriveCalPoint *out)
{
    portENTER_CRITICAL(&driveCalMux);
    const uint8_t n = driveCalCount;
    memcpy(out, driveCal, sizeof(DriveCalPoint) * n);
    portEXIT_CRITICAL(&driveCalMux);
    return n;
}

// A higher mark must need more drive (and give more Hz) than a lower one.
static bool outOfOrder(const DriveCalPoint &q, const DriveCalPoint &p)
{
    const bool below = q.speed < p.speed;
    if (below ? (q.drive >= p.drive) : (q.drive <= p.drive))
        return true;
    return q.hz > 0.0f && p.hz > 0.0f && (below ? (q.hz >= p.hz) : (q.hz <= p.hz));
}

uint8_t driveCalCapture(uint16_t speed)
{
    if (speed < 1 || speed > 400)
        return DCAL_ERR_SPEED;

    DriveCalPoint p;
    p.speed = speed;
    p.src = DCAL_SRC_EYE;
    p.rsv = 0;
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

    // Work on a copy: nothing changes unless the new mark fits.
    DriveCalPoint t[DRIVE_CAL_MAX];
    uint8_t n = driveCalCount;
    memcpy(t, driveCal, sizeof(DriveCalPoint) * n);

    // A mark set by eye beats Auto Fill's: drop the auto marks at the same speed or
    // that disagree with it (the next Auto Fill places them again).
    for (int8_t i = (int8_t)n - 1; i >= 0; i--)
    {
        if (t[i].src != DCAL_SRC_AUTO || (t[i].speed != speed && !outOfOrder(t[i], p)))
            continue;
        for (uint8_t j = (uint8_t)i; j + 1 < n; j++)
            t[j] = t[j + 1];
        n--;
    }

    // Same mark captured again: replace it.
    int8_t same = -1;
    for (uint8_t i = 0; i < n; i++)
        if (t[i].speed == speed)
            same = (int8_t)i;
    if (same < 0 && n >= DRIVE_CAL_MAX)
    {
        // Full: make room by dropping the auto mark nearest the new one.
        int8_t drop = -1;
        uint16_t best = 0xFFFF;
        for (uint8_t i = 0; i < n; i++)
        {
            const uint16_t gap = t[i].speed > speed ? t[i].speed - speed : speed - t[i].speed;
            if (t[i].src == DCAL_SRC_AUTO && gap < best)
            {
                best = gap;
                drop = (int8_t)i;
            }
        }
        if (drop < 0)
            return DCAL_ERR_FULL;
        for (uint8_t j = (uint8_t)drop; j + 1 < n; j++)
            t[j] = t[j + 1];
        n--;
    }

    for (uint8_t i = 0; i < n; i++)
        if ((int8_t)i != same && outOfOrder(t[i], p))
            return DCAL_ERR_ORDER;

    if (same >= 0)
    {
        t[same] = p;
    }
    else
    {
        uint8_t pos = n;
        while (pos > 0 && t[pos - 1].speed > speed)
        {
            t[pos] = t[pos - 1];
            pos--;
        }
        t[pos] = p;
        n++;
    }
    driveCalStore(t, n);
    DEBUG_FB("cal mark %u = %.2f Hz @ drive %.2f V (rail %.2f V x %.0f%% PWM), %u marks",
             p.speed, p.hz, p.drive, p.rail, p.pwm * 100.0f, driveCalCount);
    driveCalChanged();
    return DCAL_OK;
}

void driveCalReplace(const DriveCalPoint *pts, uint8_t n)
{
    if (n > DRIVE_CAL_MAX)
        n = DRIVE_CAL_MAX;
    driveCalStore(pts, n);
    driveCalChanged();
}

void driveCalDelete(uint8_t index)
{
    portENTER_CRITICAL(&driveCalMux);
    if (index >= driveCalCount)
    {
        portEXIT_CRITICAL(&driveCalMux);
        return;
    }
    for (uint8_t i = index; i + 1 < driveCalCount; i++)
        driveCal[i] = driveCal[i + 1];
    driveCalCount--;
    portEXIT_CRITICAL(&driveCalMux);
    driveCalChanged();
}

// Eye or auto (V5.08). A table from before V5.05 loads with every mark as "eye"
// (the field was padding then), so Auto Fill would keep them all: tap the pill to
// say which ones weren't really put on the dial by eye.
bool driveCalSetSource(uint8_t index, uint8_t src)
{
    portENTER_CRITICAL(&driveCalMux);
    const bool ok = index < driveCalCount;
    if (ok)
        driveCal[index].src = src;
    portEXIT_CRITICAL(&driveCalMux);
    if (!ok)
        return false;
    driveCalChanged();
    return true;
}

void driveCalClear()
{
    portENTER_CRITICAL(&driveCalMux);
    driveCalCount = 0;
    portEXIT_CRITICAL(&driveCalMux);
    driveCalChanged();
}

// ===== Export / import =====
// Tagged with "type" so a voltage cal file can't be mistaken for (or imported
// as) a legacy PWM-duty cal. The drive settings the marks were captured with go
// along too: the same drive splits into a different rail x PWM if they change.
void driveCalExportJson(String &out, bool pretty)
{
    JsonDocument doc;
    doc["type"] = DRIVE_CAL_FILE_TYPE;
    doc["about"] = "Voltage calibration (voltage board, Voltage Control on): needle marks vs "
                   "drive in volts (rail x PWM). Not a PWM-duty preset.";
    doc["firmware"] = VERSION;
    doc["unit"] = clusterInMph ? "mph" : "kmh";
    doc["pwmNominal"] = pwmNominal;
    doc["railMinV"] = railMinV;
    doc["railMaxV"] = railMaxV;
    JsonArray marks = doc["marks"].to<JsonArray>();
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        JsonObject m = marks.add<JsonObject>();
        m["speed"] = driveCal[i].speed;
        m["hz"] = driveCal[i].hz;
        m["drive"] = driveCal[i].drive;
        m["rail"] = driveCal[i].rail;
        m["pwm"] = driveCal[i].pwm;
        m["src"] = driveCal[i].src == DCAL_SRC_AUTO ? "auto" : "eye";
    }
    if (pretty)
        serializeJsonPretty(doc, out);
    else
        serializeJson(doc, out);
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
        // Files from before V5.05 have no "src": every mark in them was set by eye.
        p.src = (strcmp(m["src"] | "eye", "auto") == 0) ? DCAL_SRC_AUTO : DCAL_SRC_EYE;
        p.rsv = 0;
        if (p.speed < 1 || p.speed > 400 || p.drive <= 0.0f)
        {
            err = "A mark in the file has no speed or drive";
            return false;
        }
        // V5.09: a hand-edited file could ask for any drive (50 V = full drive from
        // the first quarter of the dial up). Only what the board can actually do.
        if (!(p.drive <= RAIL_V_CEIL) || !(p.hz >= 0.0f && p.hz <= 2000.0f) ||
            !(p.rail >= 0.0f && p.rail <= RAIL_V_CEIL) || !(p.pwm >= 0.0f && p.pwm <= 1.0f))
        {
            err = "A mark in the file has an impossible drive, rail, PWM or Hz";
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

    pwmNominal = constrain(doc["pwmNominal"] | pwmNominal, 0.2f, 1.0f);
    railMaxV = constrain(doc["railMaxV"] | railMaxV, RAIL_V_FLOOR, RAIL_V_CEIL);
    // Files from V5.01-V5.03 say railMinV 3 (the fixed floor then): keep that split.
    railMinV = constrain(doc["railMinV"] | RAIL_V_FLOOR, RAIL_V_FLOOR, railMaxV);
    clusterInMph = (strcmp(doc["unit"] | (clusterInMph ? "mph" : "kmh"), "mph") == 0);
    driveCalStore(tmp, n);
    driveCalChanged();
    DEBUG_FB("voltage cal imported: %u marks, %u-%u (nominal PWM %.0f%%, max rail %.1f V)",
             n, tmp[0].speed, tmp[n - 1].speed, pwmNominal * 100.0f, railMaxV);
    err = "OK";
    return true;
}

// ===== Backup (one slot) =====
// The table Auto Fill replaced, as an export file in NVS ("dcBak"): it carries the
// drive settings too, so restoring it puts back exactly what ran before.
static const char *DCAL_BACKUP_KEY = "dcBak";
bool driveCalBackupValid = false;

bool driveCalSaveBackup()
{
    if (driveCalCount == 0)
        return false;
    String js;
    driveCalExportJson(js, false);
    if (js.length() > 3900) // NVS string limit is 4000
        return false;
    driveCalBackupValid = settingsStore.putString(DCAL_BACKUP_KEY, js) > 0;
    DEBUG_FB("cal backup saved (%u marks, %u bytes)", driveCalCount, (unsigned)js.length());
    return driveCalBackupValid;
}

bool driveCalRestoreBackup(const char *&err)
{
    err = "No previous calibration saved";
    if (!settingsStore.isKey(DCAL_BACKUP_KEY))
    {
        driveCalBackupValid = false;
        return false;
    }
    const String bak = settingsStore.getString(DCAL_BACKUP_KEY, "");
    String cur;
    if (driveCalCount > 0)
        driveCalExportJson(cur, false);
    if (!driveCalImportJson(bak.c_str(), err))
        return false;
    // Swap: what was running becomes the backup, so the restore can be undone.
    if (cur.length() > 0 && cur.length() <= 3900)
        settingsStore.putString(DCAL_BACKUP_KEY, cur);
    else
        settingsStore.remove(DCAL_BACKUP_KEY);
    driveCalBackupValid = settingsStore.isKey(DCAL_BACKUP_KEY);
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
    case DCAL_ERR_FULL:      return "All 20 marks used: delete one first";
    case DCAL_ERR_SPEED:     return "Speed must be 1-400";
    default:                 return "OK";
    }
}
