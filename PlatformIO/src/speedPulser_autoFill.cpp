#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"

// ===== Auto Fill (V5.05) =====
// A calibration is two curves:
//  - the dial: tacho Hz -> where the needle points. Only a person can see that, but
//    it is a straight line (bench, 2026-11-15: ten marks 40-193 km/h captured by eye
//    all sit within 0.9 km/h of one line, and a line through just 40 + 140 predicts
//    the rest within 1.4 km/h);
//  - the motor: drive -> tacho Hz. The tacho measures that on its own.
// So the user captures only the EYE marks (at least the top one: the highest speed
// the needle may be driven to), and Auto Fill does the rest: it keeps each eye mark's
// speed and Hz, puts auto marks on the dial line through the eye marks (below the
// lowest eye mark the first segment is extended; one eye mark = a line through zero),
// and for every mark, lowest first, finds the drive that holds that Hz with the
// tacho.
//
// V5.08: the run is visible. At the start the old table is saved as the backup and
// the auto marks are cleared, leaving only the eye marks; each mark is put into the
// table as soon as it is measured, so the list builds up while you watch. A failed
// or stopped run puts the old table back (and it stays the backup).
//
// Stop requests (web page, low-power mode) only set a flag: the run is ended here,
// in the control task, so a stop can't land between this tick's checks and its
// drive write and be undone.
//
// It runs in Calibration Mode, from the control task's 100 ms tick, by setting the
// calibration drive (calibrationDriveV) the way the user's -/+ buttons do.
//
// Safety: never faster than the top eye mark (stops at topHz x 1.06 + 1 Hz on the
// raw tacho), never more drive than 1.25 x the top eye mark's capture drive or the
// Max Rail x Nominal PWM limit, each step up at most 25 % of the drive, and the motor
// is stopped when it ends, fails, or Calibration Mode / Speed Test changes.

volatile uint8_t autoFillState = AF_IDLE;
uint8_t autoFillIndex = 0;
uint8_t autoFillTotal = 0;
uint16_t autoFillSpeed = 0;
float autoFillTargetHz = 0.0f;
char autoFillMsg[160] = "";


static DriveCalPoint plan[DRIVE_CAL_MAX]; // speed, src, hz set; drive/rail/pwm filled as measured
static float measHz[DRIVE_CAL_MAX];       // what each accepted mark actually ran at
static uint8_t planN = 0;
static float eyeHz[DRIVE_CAL_MAX], eyeDrive[DRIVE_CAL_MAX];
static DriveCalPoint eyePoint[DRIVE_CAL_MAX]; // the eye marks as captured (shown until re-measured)
static uint8_t eyeN = 0;
static DriveCalPoint originalTable[DRIVE_CAL_MAX]; // put back if the run fails or is stopped
static uint8_t originalCount = 0;
static const char *volatile stopRequest = nullptr;
static float hzCeil = 0.0f, driveCap = 0.0f;

static bool measuring = false;
static uint16_t ticks = 0, waitTicks = 0;
static float drv = 0.0f, slope = AF_SLOPE0;
static uint8_t iters = 0, stalls = 0;
static bool haveLast = false;
static float lastD = 0.0f, lastHz = 0.0f;
static float bestD = 0.0f, bestHz = 0.0f, bestErr = 1e9f, bestRail = 0.0f, bestPwm = 0.0f;
static float sumHz = 0.0f;
static uint16_t nHz = 0;

// Where the dial puts a speed, from the eye marks: straight lines between them; below
// the lowest one the line from the lowest to the top eye mark (a long baseline: the
// first short segment would carry its marks' by-eye error, ~1 km/h, a long way down);
// one eye mark = a line through zero.
static float dialHz(const float *ex, const float *ey, uint8_t ne, float s)
{
    if (ne >= 2 && s < ex[0])
    {
        const float k = (ey[ne - 1] - ey[0]) / (ex[ne - 1] - ex[0]);
        const float hz = ey[0] + (s - ex[0]) * k;
        return hz > 0.0f ? hz : 0.0f;
    }
    return driveCalInterp(ex, ey, ne, s);
}

static bool nearEye(uint16_t s, const float *ex, uint8_t ne)
{
    for (uint8_t i = 0; i < ne; i++)
    {
        const float gap = fabsf((float)s - ex[i]);
        if (gap < (float)AF_MIN_GAP)
            return true;
    }
    return false;
}

// The marks to measure: every eye mark, plus auto marks on the dial line.
static bool buildPlan(const char *&err)
{
    float ex[DRIVE_CAL_MAX], ey[DRIVE_CAL_MAX];
    eyeN = 0;
    for (uint8_t i = 0; i < driveCalCount; i++)
    {
        const DriveCalPoint &p = driveCal[i];
        if (p.src == DCAL_SRC_AUTO)
            continue;
        if (p.hz <= 0.0f)
        {
            snprintf(autoFillMsg, sizeof autoFillMsg,
                     "The %u mark has no motor Hz: capture it again (tacho needed)", p.speed);
            err = autoFillMsg;
            return false;
        }
        ex[eyeN] = p.speed;
        ey[eyeN] = p.hz;
        eyeHz[eyeN] = p.hz;
        eyeDrive[eyeN] = p.drive;
        eyePoint[eyeN] = p;
        eyeN++;
    }
    if (eyeN == 0)
    {
        err = "Capture the top mark first: put the needle on it by eye and press Capture Mark";
        return false;
    }
    const uint16_t top = (uint16_t)ex[eyeN - 1];

    uint16_t grid[DRIVE_CAL_MAX];
    uint8_t ng = 0;
    for (uint8_t si = 0; si < sizeof AF_STEPS / sizeof AF_STEPS[0]; si++)
    {
        ng = 0;
        bool fits = true;
        uint16_t cand[64];
        uint8_t nc = 0;
        for (uint8_t k = 0; k < sizeof AF_LOW_GRID / sizeof AF_LOW_GRID[0]; k++)
            cand[nc++] = AF_LOW_GRID[k];
        for (uint16_t s = 40 + AF_STEPS[si]; s < top && nc < 64; s += AF_STEPS[si])
            cand[nc++] = s;
        for (uint8_t k = 0; k < nc; k++)
        {
            const uint16_t s = cand[k];
            if (s >= top || nearEye(s, ex, eyeN))
                continue;
            if (dialHz(ex, ey, eyeN, (float)s) < AF_MIN_HZ)
                continue;
            if (eyeN + ng >= DRIVE_CAL_MAX)
            {
                fits = false;
                break;
            }
            grid[ng++] = s;
        }
        if (fits)
            break;
        if (si == sizeof AF_STEPS / sizeof AF_STEPS[0] - 1)
            ng = 0; // the eye marks alone fill the table: just re-measure them
    }

    // Merge, ascending.
    planN = 0;
    uint8_t ie = 0, ig = 0;
    while (ie < eyeN || ig < ng)
    {
        DriveCalPoint p = {};
        if (ig >= ng || (ie < eyeN && ex[ie] < grid[ig]))
        {
            p = eyePoint[ie]; // its captured drive is shown until it is re-measured
            p.src = DCAL_SRC_EYE;
            ie++;
        }
        else
        {
            p.speed = grid[ig];
            p.src = DCAL_SRC_AUTO;
            p.hz = dialHz(ex, ey, eyeN, (float)grid[ig]);
            ig++;
        }
        plan[planN++] = p;
    }
    hzCeil = ey[eyeN - 1] * 1.06f + 1.0f;
    driveCap = eyeDrive[eyeN - 1] * 1.25f;
    if (driveCap > driveMaxV())
        driveCap = driveMaxV();
    return true;
}

static void finish(uint8_t state)
{
    calibrationDriveV = 0.0f;
    measuring = false;
    stopRequest = nullptr;
    if (state == AF_FAILED)
        driveCalReplace(originalTable, originalCount); // the run didn't finish: the old table back
    autoFillState = state;
}

// The live table while running: the marks measured so far plus the eye marks still
// to come (with their captured drive), so the page shows the table building up.
static void showProgress(uint8_t measured)
{
    DriveCalPoint shown[DRIVE_CAL_MAX];
    uint8_t n = 0;
    for (uint8_t j = 0; j < planN; j++)
        if (j < measured || plan[j].src == DCAL_SRC_EYE)
            shown[n++] = plan[j];
    driveCalReplace(shown, n);
}

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(autoFillMsg, sizeof autoFillMsg, fmt, ap);
    va_end(ap);
    strlcat(autoFillMsg, ". Previous table put back", sizeof autoFillMsg);
    DEBUG_FB("auto fill: %s", autoFillMsg);
    finish(AF_FAILED);
}

static void startPoint(uint8_t i);

// A low auto mark the motor can't hold (at its floor) is left out rather than failing
// the run: below the lowest mark the table's first segment is extended anyway.
static bool skipLowAuto(const char *why)
{
    const uint8_t i = autoFillIndex;
    if (plan[i].src != DCAL_SRC_AUTO || plan[i].hz >= AF_SKIP_HZ || planN < 2)
        return false;
    DEBUG_FB("auto fill: %u left out (%s)", plan[i].speed, why);
    for (uint8_t j = i; j + 1 < planN; j++)
    {
        plan[j] = plan[j + 1];
        measHz[j] = measHz[j + 1];
    }
    planN--;
    autoFillTotal = planN;
    startPoint(i);
    return true;
}

static void startPoint(uint8_t i)
{
    autoFillIndex = i;
    autoFillSpeed = plan[i].speed;
    autoFillTargetHz = plan[i].hz;
    const float hz = plan[i].hz;
    if (i == 0)
    {
        // From the eye marks' own drives (maybe captured with other drive settings:
        // only a first guess).
        drv = driveCalInterp(eyeHz, eyeDrive, eyeN, hz);
        waitTicks = AF_SETTLE_START;
    }
    else
    {
        if (i >= 2)
        {
            const float dd = plan[i - 1].drive - plan[i - 2].drive;
            const float dh = measHz[i - 1] - measHz[i - 2];
            if (dd > 0.002f && dh > 0.05f)
                slope = constrain(dh / dd, AF_SLOPE_MIN, AF_SLOPE_MAX);
        }
        drv = plan[i - 1].drive + (hz - measHz[i - 1]) / slope;
        waitTicks = hz < 25.0f ? AF_SETTLE_LOW : AF_SETTLE;
        if (i > 0 && drv <= plan[i - 1].drive)
            drv = plan[i - 1].drive * 1.02f;
    }
    if (drv < 0.02f)
        drv = 0.02f;
    if (drv > driveCap)
        drv = driveCap;
    calibrationDriveV = drv;
    measuring = false;
    ticks = 0;
    iters = 0;
    stalls = 0;
    haveLast = false;
    bestErr = 1e9f;
}

bool autoFillStart(const char *&err)
{
    if (!boardHasVoltageControl)
    {
        err = "Auto Fill needs a voltage board";
        return false;
    }
    if (boardTestActive)
    {
        err = "Board test running";
        return false;
    }
    if (autoFillRunning())
    {
        err = "Auto Fill is already running";
        return false;
    }
    if (inputSpeedKph > 0.0f)
    {
        err = "Stop the car first: Auto Fill takes over the speedo";
        return false;
    }
    if (!buildPlan(err))
        return false;
    originalCount = driveCalCount;
    memcpy(originalTable, driveCal, sizeof(DriveCalPoint) * driveCalCount);
    driveCalSaveBackup(); // the table being replaced
    stopRequest = nullptr;
    snprintf(autoFillMsg, sizeof autoFillMsg, "Running: %u marks, %u-%u", planN, plan[0].speed,
             plan[planN - 1].speed);
    DEBUG_FB("auto fill: %u marks (%u eye), %u-%u, ceiling %.1f Hz, drive cap %.2f V", planN, eyeN,
             plan[0].speed, plan[planN - 1].speed, hzCeil, driveCap);
    speedTestOn = false;
    calibrationModeOn = true;
    resetDriveControl();
    slope = AF_SLOPE0;
    autoFillTotal = planN;
    startPoint(0);
    showProgress(0); // auto marks cleared: only the eye marks are left
    autoFillState = AF_RUNNING; // last: the control task starts on its next tick
    err = "OK";
    return true;
}

void autoFillStop(const char *why)
{
    if (autoFillRunning())
        stopRequest = why ? why : "Stopped"; // acted on by the next autoFillTick()
}

static void accept()
{
    const uint8_t i = autoFillIndex;
    if (i > 0 && bestD <= plan[i - 1].drive)
    {
        fail("Out of order at %u: it needed no more drive than %u", plan[i].speed, plan[i - 1].speed);
        return;
    }
    plan[i].drive = bestD;
    plan[i].rail = bestRail;
    plan[i].pwm = bestPwm;
    plan[i].rsv = 0;
    measHz[i] = bestHz;
    DEBUG_FB("auto fill: %u = %.2f Hz at %.3f V (measured %.2f Hz, %u tries)", plan[i].speed,
             plan[i].hz, bestD, bestHz, iters);
    if (i + 1 < planN)
    {
        showProgress(i + 1);
        startPoint(i + 1);
        return;
    }
    driveCalReplace(plan, planN);
    snprintf(autoFillMsg, sizeof autoFillMsg, "Done: %u marks, %u-%u (%u by eye)", planN,
             plan[0].speed, plan[planN - 1].speed, eyeN);
    DEBUG_FB("auto fill: %s", autoFillMsg);
    finish(AF_DONE);
}

void autoFillTick()
{
    if (!autoFillRunning())
        return;
    if (stopRequest)
    {
        fail("%s", stopRequest);
        return;
    }
    if (!calibrationModeOn || speedTestOn || boardTestActive)
    {
        fail("Stopped: Calibration Mode was turned off");
        return;
    }
    const float raw = measuredFreqRawHz;
    if (raw > hzCeil)
    {
        fail("Stopped: the motor ran faster than the top mark (%.0f Hz > %.0f Hz)", raw, hzCeil);
        return;
    }
    const float tgt = plan[autoFillIndex].hz;
    ticks++;

    if (!measuring)
    {
        if (ticks < waitTicks)
            return;
        if (raw <= 0.0f)
        {
            // Not turning: more drive, a little at a time.
            if (++stalls > AF_MAX_STALLS || drv >= driveCap)
            {
                if (skipLowAuto("not turning"))
                    return;
                fail("Motor not turning at %.2f V drive (mark %u): check the tacho and Minimum Rail",
                     drv, plan[autoFillIndex].speed);
                return;
            }
            drv = drv * 1.3f + 0.03f;
            if (drv > driveCap)
                drv = driveCap;
            calibrationDriveV = drv;
            ticks = 0;
            waitTicks = AF_SETTLE_START;
            return;
        }
        measuring = true;
        ticks = 0;
        sumHz = 0.0f;
        nHz = 0;
        return;
    }

    if (raw <= 0.0f)
    {
        measuring = false; // stalled while averaging: let it settle again
        ticks = 0;
        return;
    }
    sumHz += raw;
    nHz++;
    if (nHz < (tgt < 25.0f ? AF_HOLD_LOW : AF_HOLD))
        return;

    const float m = sumHz / (float)nHz;
    const float err = tgt - m;
    if (fabsf(err) < fabsf(bestErr))
    {
        bestErr = err;
        bestD = drv;
        bestHz = m;
        bestRail = lastRailCmdV;
        bestPwm = lastPwmFrac;
    }
    if (haveLast && fabsf(drv - lastD) > 0.002f && fabsf(m - lastHz) > 0.05f)
    {
        const float s = (m - lastHz) / (drv - lastD);
        if (s > 0.0f)
            slope = constrain(s, AF_SLOPE_MIN, AF_SLOPE_MAX);
    }
    lastD = drv;
    lastHz = m;
    haveLast = true;
    iters++;

    const float tol = fmaxf(AF_TOL_HZ, AF_TOL_FRAC * tgt);
    if (fabsf(err) <= tol)
    {
        accept();
        return;
    }
    if (iters >= AF_MAX_ITERS)
    {
        if (fabsf(bestErr) <= AF_ACCEPT_FRAC * tgt + AF_TOL_HZ)
            accept();
        else if (!skipLowAuto("can't hold it"))
            fail("Couldn't hold %u: best %.1f Hz of %.1f Hz", plan[autoFillIndex].speed, bestHz, tgt);
        return;
    }

    // Up by at most 25 % a try (towards the needle's limit), down freely (safe) but
    // not below 30 % of the drive in one go (it would only stall and restart).
    const float maxStep = fmaxf(AF_STEP_FRAC * drv, AF_STEP_MIN_V);
    float nd = drv + err / slope;
    if (nd > drv + maxStep)
        nd = drv + maxStep;
    if (nd < drv * 0.3f)
        nd = drv * 0.3f;
    if (nd < 0.01f)
        nd = 0.01f;
    if (nd > driveCap)
    {
        if (drv >= driveCap - 0.0005f)
        {
            fail("Mark %u needs more drive than allowed (%.2f V): check Max Rail / Nominal PWM",
                 plan[autoFillIndex].speed, driveCap);
            return;
        }
        nd = driveCap;
    }
    drv = nd;
    calibrationDriveV = drv;
    measuring = false;
    ticks = 0;
    waitTicks = tgt < 25.0f ? AF_SETTLE_LOW : AF_SETTLE;
}
