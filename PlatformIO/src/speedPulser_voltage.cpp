#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"

// ---------------------------------------------------------------------------
// SpeedPulser — buck voltage-control hardware layer.
// See speedPulser_voltage.h for the design overview. This file owns the board
// detection, the V_ADJ PWM "DAC", the buck-enable pin, the drive -> rail + PWM
// split, the motor-rail read-back and the Test Board routine. The speed -> drive
// calibration lives in speedPulser_driveCal.cpp and the control loop in
// speedPulser_control.cpp.
// ---------------------------------------------------------------------------

// ===== Board detection + live state =====
bool  boardHasVoltageControl = false;
bool  buckEnabled = false;
float lastRailCmdV = 0.0f;
float lastPwmFrac = 0.0f;
float lastDriveV = 0.0f;
float motorRailV = 0.0f;
float motorRailRawV = 0.0f; // last unsmoothed rail read (V)
float railTripV = 0.0f;      // the reading that tripped the over-voltage check
static uint32_t railCmdChangedMs = 0; // when the rail command last moved

// ===== User configuration (defaults; overwritten from EEP) =====
float pwmNominal = 0.80f; // PWM held while the rail does the work
float railMaxV   = RAIL_V_MAX_DEFAULT; // V5.08: 8 V for a new unit (was the 12 V ceiling); up to 12 V allowed
float railMinV   = RAIL_V_MIN_DEFAULT; // rail held at the low end (V5.04); readEEP keeps 3.0 for old cals
float trimGain   = 2.00f; // V5.04 bench-tuned: a 10 % speed error moves the drive 20 %/s
                            // (0.5 took ~7 s to recover a load step; 3+ starts to wobble)
float trimLimit  = 0.40f; // feedback may move the calibrated drive by at most ±40 % (a 0.1 V
                            // rail shift needs ~30 % at 20 km/h; the top mark is capped at +5 %)

// ===== Calibration Mode drive (V) =====
float calibrationDriveV = 0.0f;

// ===== Board test =====
volatile bool boardTestActive = false;
uint8_t boardTestCode = BT_NOT_RUN;
float   boardTestTargetV[BOARD_TEST_POINTS] = {5.0f, 7.5f, 10.0f}; // ascending: the buck can only
                                                                   // pull the rail UP quickly
float   boardTestMeasV[BOARD_TEST_POINTS] = {0.0f, 0.0f, 0.0f};
bool    railFitValid = false;
float   railFitV0 = RAIL_MODEL_V0;
float   railFitSlope = RAIL_MODEL_SLOPE;

// Read the board-version strap once at boot. The voltage board ties GPIO3 to GND
// via 1k, so with the internal pull-up (~45k) it reads LOW (~0.07 V); a legacy
// board has no resistor and floats HIGH. Passive and instant, so it is safe to
// run on every boot. Sampled before GPIO3 is used for anything else.
void detectBoardVersion()
{
    pinMode(pinBoardVersion, INPUT_PULLUP);
    delayMicroseconds(50); // let the node settle through the internal pull-up
    boardHasVoltageControl = (digitalRead(pinBoardVersion) == LOW);
    if (boardHasVoltageControl)
        pinMode(pinBoardVersion, INPUT); // the 1k holds it LOW: the pull-up would only waste ~72 uA (V5.09)
    DEBUG_IO("board version: GPIO%d=%s -> %s", pinBoardVersion,
             boardHasVoltageControl ? "LOW" : "HIGH",
             boardHasVoltageControl ? "voltage board (buck voltage control)" : "legacy (PWM only)");
}

// ===== Rail volts -> V_ADJ duty =====
// Through this board's measured line when the Test Board has passed, otherwise
// the design-nominal model. INVERSE: more duty = lower rail.
static float vadjDutyForVolts(float volts, bool useFit)
{
    const float v0 = useFit ? railFitV0 : RAIL_MODEL_V0;
    const float k = useFit ? railFitSlope : RAIL_MODEL_SLOPE;
    float d = (k < -0.1f) ? (volts - v0) / k : 1.0f; // guard a nonsense slope -> lowest rail
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    return d;
}

#ifdef RAIL_HARD_MAX_V
// Bench build: the ONE place the rail is set, so this cap covers every path
// (legacy 9 V hold, drive split, needle sweep, calibration). Higher V_ADJ duty =
// LOWER rail, so cap by a minimum duty — the larger of what the nominal model and
// the board's own fit need to stay at or under RAIL_HARD_MAX_V.
static float vadjMinDutyForCap()
{
    float dMin = (RAIL_HARD_MAX_V - RAIL_MODEL_V0) / RAIL_MODEL_SLOPE;
    if (railFitValid && railFitSlope < -0.1f)
    {
        const float dFit = (RAIL_HARD_MAX_V - railFitV0) / railFitSlope;
        if (dFit > dMin) dMin = dFit;
    }
    return dMin > 1.0f ? 1.0f : dMin;
}
#endif

static void writeVadjDuty(float d01)
{
#ifdef RAIL_HARD_MAX_V
    const float dMin = vadjMinDutyForCap();
    if (d01 < dMin) d01 = dMin;
#endif
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_VADJ, (uint32_t)(d01 * (float)VADJ_DUTY_MAX + 0.5f));
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_VADJ);
}

void setMotorRailVolts(float volts)
{
    if (!boardHasVoltageControl) return;
    if (volts < RAIL_V_FLOOR) volts = RAIL_V_FLOOR;
    if (volts > RAIL_V_CEIL) volts = RAIL_V_CEIL;
#ifdef RAIL_HARD_MAX_V
    if (volts > RAIL_HARD_MAX_V) volts = RAIL_HARD_MAX_V;
#endif
    if (fabsf(volts - lastRailCmdV) > 0.2f)
        railCmdChangedMs = millis(); // the over-voltage check waits for the rail to follow
    lastRailCmdV = volts;
    writeVadjDuty(vadjDutyForVolts(volts, railFitValid));
}

// ===== Drive -> rail + PWM =====
// V5.04: Minimum Rail is a setting again (default RAIL_V_MIN_DEFAULT). Bench
// tests on the TY3816B (2026-11-15, on a cluster) showed the motor's own driver
// only behaves properly with a rail of ~4.75 V or more: from 3.1 to 4.6 V low
// speeds jump, stall and won't restart, while at 4.8 V the PWM alone runs it
// smoothly from ~2 % (8 Hz) up, starting from rest every time. So the low end is
// now run on a fixed, healthy rail with the PWM doing the work.
// Maximum Rail is the user's, kept inside what the hardware can make.
static float railHiV() { return constrain(railMaxV, RAIL_V_FLOOR, RAIL_V_CEIL); }
static float railLoV() { return constrain(railMinV, RAIL_V_FLOOR, railHiV()); }

float driveMaxV()
{
    return railHiV(); // Maximum Rail x 100 % PWM
}

// One drive number (V = rail x PWM) -> the rail and PWM that make it. Between
// Min x Nominal and Max x Nominal the PWM stays at Nominal and only the rail
// moves; below, the rail stays at Minimum and the PWM falls to zero; above, the
// rail stays at Maximum and the PWM rises to 100 %. Continuous and monotonic, so
// calibration can interpolate it.
uint16_t applyMotorDrive(float driveV)
{
    if (!boardHasVoltageControl) return 0;
    const float lo = railLoV();
    const float hi = railHiV();
    const float pn = (pwmNominal > 0.05f) ? pwmNominal : 0.05f;
    if (driveV < 0.0f) driveV = 0.0f;
    if (driveV > hi) driveV = hi;

    float rail = driveV / pn;
    float pwm = pn;
    if (rail < lo)
    {
        rail = lo;
        pwm = driveV / lo;
    }
    else if (rail > hi)
    {
        rail = hi;
        pwm = driveV / hi;
    }
    if (pwm > 1.0f) pwm = 1.0f;
    if (driveV <= 0.0f)
        rail = RAIL_V_FLOOR; // motor off: idle the rail at the floor, not at Minimum Rail

    lastDriveV = driveV;
    lastPwmFrac = pwm;
    setMotorRailVolts(rail);
    uint32_t duty = (uint32_t)(pwm * (float)PWM_DUTY_MAX + 0.5f);
    if (duty > PWM_DUTY_MAX) duty = PWM_DUTY_MAX;
    setMotorDutyRaw(duty);
    return (uint16_t)duty;
}

void initVoltageControl()
{
    if (!boardHasVoltageControl) return;

    DEBUG_IO("setting up buck voltage control...");

    // Buck enable pin: default OFF (LOW) until a safe voltage is set and we enable it.
    pinMode(pinBuckEnable, OUTPUT);
    digitalWrite(pinBuckEnable, LOW);
    buckEnabled = false;

    // V_ADJ PWM "DAC" on its own LEDC timer/channel.
    ledc_timer_config_t vadj_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)VADJ_RESOLUTION,
        .timer_num = LEDC_TIMER_VADJ,
        .freq_hz = VADJ_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&vadj_timer);

    ledc_channel_config_t vadj_channel = {
        .gpio_num = pinVoltageAdjust,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_VADJ,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_VADJ,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&vadj_channel);

    // Rail read-back: 11 dB attenuation covers the divider's 0..2.45 V (0..12 V rail).
    // GPIO1 is ADC1 — ADC2 is unusable while WiFi is up.
    analogSetPinAttenuation(pinMotorSense, ADC_11db);

    // Start at the hardware floor (safe) before the buck is enabled.
    setMotorRailVolts(RAIL_V_FLOOR);
    DEBUG_IO("voltage control ready (V_ADJ GPIO%d, EN GPIO%d, sense GPIO%d) — buck OFF, V=min (%.2f V, %s)",
             pinVoltageAdjust, pinBuckEnable, pinMotorSense, lastRailCmdV,
             railFitValid ? "board fit" : "nominal model");
}

// Latched when the rail reads too high (see updateMotorRail). Motor off, buck off,
// and enableBuck() refuses until a reboot.
volatile bool railTripped = false;

void enableBuck()
{
    if (!boardHasVoltageControl || buckEnabled) return;
    if (railTripped) return;
    setMotorRailVolts(RAIL_V_FLOOR);   // guarantee a safe (low) rail first
    digitalWrite(pinBuckEnable, HIGH); // GPIO7 HIGH = enable
    buckEnabled = true;
    DEBUG_IO("buck enabled (GPIO%d HIGH)", pinBuckEnable);
}

void disableBuck()
{
    if (!boardHasVoltageControl) return;
    digitalWrite(pinBuckEnable, LOW);
    buckEnabled = false;
    DEBUG_IO("buck disabled (GPIO%d LOW)", pinBuckEnable);
}

// ===== Motor-rail read-back (V_MOT_SENSE) =====
// eFuse-calibrated millivolt read, averaged; C33 (100 nF) at the pin keeps the
// SAR's sampling kick out of the reading.
static float readRailAveraged(int samples)
{
    uint32_t sumMv = 0;
    for (int i = 0; i < samples; i++)
        sumMv += analogReadMilliVolts(pinMotorSense);
    const float pinV = (float)sumMv / (float)samples / 1000.0f;
    return pinV * (RAIL_SENSE_R_TOP_K + RAIL_SENSE_R_BOT_K) / RAIL_SENSE_R_BOT_K;
}

float readMotorRailVolts()
{
    if (!boardHasVoltageControl) return 0.0f; // legacy board: GPIO1 is not ours to sample
    return readRailAveraged(8);
}

void updateMotorRail()
{
    if (!boardHasVoltageControl) return;
    const float v = readMotorRailVolts();
    motorRailRawV = v;

    // Over-voltage (V5.08, every build): the rail well above its command for half a
    // second means the V_ADJ injection has failed (GPIO6 stuck low leaves the rail
    // near 13.8 V). The buck can only push the rail up, so with the motor off a
    // lowered rail takes seconds to fall: the command must have been steady for
    // RAIL_OV_SETTLE_MS before a high reading counts.
    static uint8_t overTicks = 0;
    const bool over = buckEnabled && (millis() - railCmdChangedMs) > RAIL_OV_SETTLE_MS &&
                      v > lastRailCmdV + RAIL_OV_MARGIN_V;
    overTicks = over ? (uint8_t)(overTicks < 255 ? overTicks + 1 : 255) : 0;
    if (overTicks >= RAIL_OV_TICKS && !railTripped)
    {
        setMotorDutyRaw(0);
        disableBuck();
        railTripped = true;
        railTripV = v;
        DEBUG_IO("rail over-voltage: %.2f V with %.2f V commanded - motor off, buck off (reboot to clear)",
                 v, lastRailCmdV);
    }
#ifdef RAIL_TRIP_V
    if (v > RAIL_TRIP_V && !railTripped)
    {
        setMotorDutyRaw(0);
        disableBuck();
        railTripped = true;
        Serial.printf("TRIP rail %.2f V > %.2f V - motor off, buck off (reboot to clear)\n",
                      v, (float)RAIL_TRIP_V);
    }
#endif
    motorRailV += 0.3f * (v - motorRailV); // light EMA for a steady UI read-out
}

// Rail more than 0.8 V under its command: the supply is too low to reach it (buck
// in dropout) or the motor is overloading the rail. Only meaningful once the
// Test Board has proven the read-back.
bool railLimited()
{
    return railFitValid && buckEnabled && (lastRailCmdV - motorRailV) > 0.8f;
}

// ===== Test Board =====
// Steps the rail through three ascending targets with the motor PWM held at 0, and
// reads each back on GPIO1. Proves the V_ADJ injection and the sense divider in
// one go, and fits this board's own duty->volts line (resistor / Vref / 3V3
// tolerances) so later rail commands land on the volts asked for.

// Wait for the rail to settle near a step, then return an averaged reading.
// Settled = moved < 20 mV across the last 250 ms. Going UP the buck drives the
// rail quickly; coming DOWN with no motor load only the 49k divider discharges
// the ~66 µF on the rail (tau ~3 s), so the first step gets a longer timeout.
static float settleRail(uint32_t maxMs, bool &settled)
{
    const uint32_t SAMPLE_MS = 50;
    const uint8_t WINDOW = 5; // 5 x 50 ms = 250 ms
    float hist[WINDOW] = {0};
    uint8_t n = 0;

    vTaskDelay(pdMS_TO_TICKS(150));
    const uint32_t start = millis();
    settled = false;
    while ((millis() - start) < maxMs)
    {
        const float v = readRailAveraged(8);
        if (n >= WINDOW && fabsf(v - hist[n % WINDOW]) < 0.02f)
        {
            settled = true;
            break;
        }
        hist[n % WINDOW] = v;
        n++;
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
    return readRailAveraged(32);
}

static void boardTestTask(void *)
{
    DEBUG_IO("board test: start");

    // Motor stays still for the whole test (the rail alone doesn't move the needle).
    setMotorDutyRaw(0);
    if (!buckEnabled) enableBuck();

    float duty[BOARD_TEST_POINTS];
    bool used[BOARD_TEST_POINTS];
    for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
    {
        // Drive by the NOMINAL model: we're measuring the raw relationship.
        duty[i] = vadjDutyForVolts(boardTestTargetV[i], false);
        writeVadjDuty(duty[i]);
        bool settled = false;
        boardTestMeasV[i] = settleRail(i == 0 ? 6000 : 2000, settled);
        // A first step that never settled is still discharging from a higher rail:
        // keep the reading for display but leave it out of the fit.
        used[i] = settled || i > 0;
        DEBUG_IO("board test: step %u target=%.2f V duty=%.3f -> %.2f V%s", i,
                 boardTestTargetV[i], duty[i], boardTestMeasV[i], settled ? "" : " (not settled)");
    }

    // Least-squares line through the usable points: V = v0 + k * duty.
    float sx = 0, sy = 0, sxx = 0, sxy = 0, vMax = 0;
    uint8_t n = 0, first = BOARD_TEST_POINTS, last = 0;
    for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
    {
        if (boardTestMeasV[i] > vMax) vMax = boardTestMeasV[i];
        if (!used[i]) continue;
        sx += duty[i]; sy += boardTestMeasV[i];
        sxx += duty[i] * duty[i]; sxy += duty[i] * boardTestMeasV[i];
        if (i < first) first = i;
        last = i;
        n++;
    }
    const float den = (float)n * sxx - sx * sx;
    const float k = (n >= 2 && fabsf(den) > 1e-6f) ? ((float)n * sxy - sx * sy) / den : 0.0f;
    const float v0 = (n >= 1) ? (sy - k * sx) / (float)n : 0.0f;

    const float expectedRise = boardTestTargetV[last] - boardTestTargetV[first];
    const float measuredRise = boardTestMeasV[last] - boardTestMeasV[first];
    float worstErr = 0.0f;
    for (uint8_t i = 0; i < BOARD_TEST_POINTS; i++)
    {
        if (!used[i]) continue;
        const float e = fabsf(boardTestMeasV[i] - boardTestTargetV[i]);
        if (e > worstErr) worstErr = e;
    }

    uint8_t code;
    if (vMax < 0.5f)
        code = BT_NO_SENSE;
    else if (n < 2 || measuredRise < 0.4f * expectedRise)
        code = BT_NO_RESPONSE;
    else if (worstErr > 1.5f || k > -6.0f || k < -16.0f)
        code = BT_OUT_OF_RANGE;
    else
        code = BT_PASS;

    boardTestCode = code;
    railFitValid = (code == BT_PASS);
    if (railFitValid)
    {
        railFitV0 = v0;
        railFitSlope = k;
    }
    else
    {
        railFitV0 = RAIL_MODEL_V0;
        railFitSlope = RAIL_MODEL_SLOPE;
    }
    motorRailV = boardTestMeasV[BOARD_TEST_POINTS - 1];
    DEBUG_IO("board test: %s (fit V0=%.2f k=%.3f, worst err %.2f V)",
             boardTestText(code), v0, k, worstErr);

    // Hand the rail back exactly as boot leaves it; the control task takes over.
    applyMotorDrive(0.0f);
    boardTestActive = false; // results are persisted by the periodic writeEEP
    vTaskDelete(NULL);
}

bool startBoardTest()
{
    if (boardTestActive) return false;
#ifdef RAIL_HARD_MAX_V
    return false; // bench build: the test steps the rail to 7.5 / 10 V — never here
#endif
    if (!boardHasVoltageControl)
    {
        boardTestCode = BT_LEGACY; // nothing to drive: the strap already says PWM-only
        return true;
    }
    // Steps up to BOARD_TEST_TOP_V, but never past Maximum rail (V5.09): a motor kept
    // to a lower rail on purpose mustn't get 10 V from the test. The web handler
    // refuses the test below BOARD_TEST_MIN_TOP_V (too short a line to fit).
    const float top = railMaxV < BOARD_TEST_TOP_V ? railMaxV : BOARD_TEST_TOP_V;
    boardTestTargetV[0] = BOARD_TEST_BOTTOM_V;
    boardTestTargetV[1] = (BOARD_TEST_BOTTOM_V + top) / 2.0f;
    boardTestTargetV[2] = top;
    boardTestActive = true; // the control task yields the motor + rail from here
    if (xTaskCreatePinnedToCore(boardTestTask, "boardTest", 4096, NULL,
                                tskIDLE_PRIORITY + 1, NULL, 0) != pdPASS)
    {
        boardTestActive = false;
        return false;
    }
    return true;
}

const char *boardTestText(uint8_t code)
{
    switch (code)
    {
    case BT_PASS:         return "Pass: voltage injection and rail sense working";
    case BT_LEGACY:       return "Legacy board: no voltage control fitted";
    case BT_NO_SENSE:     return "Fail: no rail voltage read back (no V_MOT_SENSE divider, or buck not running)";
    case BT_NO_RESPONSE:  return "Fail: rail does not follow V_ADJ (injection path open?)";
    case BT_OUT_OF_RANGE: return "Fail: rail responds but is off the expected voltages (check FB resistors / supply >= 12 V)";
    default:              return "Not run";
    }
}
