#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"
#include "speedPulser_calBuilder.h"

void basicInit()
{
    DEBUG_IO("initialising SpeedPulser I/O...");

    pinMode(pinOnboardLED, OUTPUT);
    digitalWrite(pinOnboardLED, LOW);

    // Motor PWM: native LEDC (Arduino-ESP32 3.x's ledcWrite() is pin-based and
    // doesn't know channels set up this way, so the duty is written the IDF way too).
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_MOTOR,
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = pinMotorOutput,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_MOTOR,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_MOTOR,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);
    DEBUG_IO("LEDC PWM ready (GPIO%d, %d-bit @ %d Hz)", pinMotorOutput, PWM_RESOLUTION, PWM_FREQUENCY);

    updateInputGlitchFilter();
    attachInterrupt(digitalPinToInterrupt(pinSpeedInput), speedInputPulse, FALLING);
    DEBUG_IO("speed input ready (GPIO%d, FALLING)", pinSpeedInput);

    pinMode(pinDirection, OUTPUT);
    applyDirection();
    DEBUG_IO("direction output ready (GPIO%d, %s)", pinDirection, reverseDirection ? "REV" : "FWD");

    pinMode(pinFeedback, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pinFeedback), feedbackPulse, FALLING);
    DEBUG_IO("tacho input ready (GPIO%d, FALLING, pull-up)", pinFeedback);
}

// Default (unticked) drives LOW = the correct direction; ticking Reverse drives HIGH.
// (V4.03-V5.00 had this the other way round; the setting moved to a new key so
// those units start unticked.)
void applyDirection()
{
    digitalWrite(pinDirection, reverseDirection ? HIGH : LOW);
}

// Write a duty straight to the PWM hardware (0..PWM_DUTY_MAX, clamped).
void setMotorDutyRaw(uint32_t pwmDuty)
{
    if (pwmDuty > PWM_DUTY_MAX)
        pwmDuty = PWM_DUTY_MAX;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_MOTOR, pwmDuty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_MOTOR);
}

// ===== Needle sweep =====
// The needle follows the motor's SPEED, and speed isn't a straight line against
// drive or duty: nothing until the motor breaks away, then a steep low end, then
// a flatter top. Up to V5.06 the sweep ramped the drive (or duty) evenly, so the
// needle leapt off the stop, slowed in the middle and rushed at the end. Now the
// sweep ramps the dial SPEED evenly and looks up the output for it through the
// calibration on every tick: the calibration is the measured inverse of that bent
// curve, so an even speed ramp gives an even needle (V5.07).
//
// One climb: from the lowest calibrated speed at a steady needleSweepSpeed (ms
// per dial km/h or mph), then the last SWEEP_EASE of the time slowing to a stop,
// so the weighted needle isn't flung at the top. It starts at full pace: the
// motor's own lag (~0.35 s) already eases the needle off the stop. At the top it
// waits for the motor to get there (tacho) or a fixed hold (no tacho). The way
// down is the climb played backwards: it eases away from the top, runs steadily
// to the lowest speed, then the motor is cut and coasts the needle home.
//
// Range: only where the calibration has proven the needle goes, the lowest to
// the highest calibrated speed (voltage board: the drive marks; legacy board: the
// selected preset or the custom table). No calibration = no sweep.

// Position 0..1 along the climb at time 0..1: a steady rate, then a cosine
// slow-down to rest. The steady rate is set so the climb still ends at 1.
static float sweepProfile(float t)
{
    const float rate = 1.0f / (1.0f - SWEEP_EASE / 2.0f);
    const float easeFrom = 1.0f - SWEEP_EASE;
    if (t <= easeFrom)
        return rate * t;
    const float u = (t - easeFrom) / SWEEP_EASE; // 0..1 through the slow-down
    return rate * (easeFrom + SWEEP_EASE * (u / 2.0f + sinf(PI * u) / (2.0f * PI)));
}

// The calibrated dial range. false = no calibration to sweep through.
static bool sweepRange(uint16_t &lowSpeed, uint16_t &topSpeed)
{
    if (boardHasVoltageControl)
    {
        if (driveCalCount == 0)
            return false;
        lowSpeed = driveCal[0].speed;
        topSpeed = driveCalTopSpeed();
    }
    else
    {
        topSpeed = 0;
        if (presetId == CUSTOM_CAL_ID && customCalValid)
            topSpeed = customCalPoints[customCalCount - 1].speed;
        else
            for (uint16_t s : presetSpeedTable)
                if (s > topSpeed)
                    topSpeed = s;
        lowSpeed = 1; // the first speed the table gives any duty for
        while (lowSpeed < topSpeed && speedToPwmDuty(lowSpeed) == 0)
            lowSpeed++;
    }
    if (lowSpeed >= topSpeed)
        lowSpeed = 0; // a single drive mark: its line runs through zero
    return topSpeed > 0;
}

// Drive the motor for a (fractional) dial speed through the calibration.
static void sweepOutput(float speed)
{
    if (boardHasVoltageControl)
    {
        motorDutyApplied = applyMotorDrive(driveForSpeed(speed));
        return;
    }
    // Legacy tables work in whole speeds: blend the duty of the two either side.
    const uint16_t whole = (uint16_t)speed;
    const float dutyLow = (float)speedToPwmDuty(whole);
    const float dutyHigh = (float)speedToPwmDuty(whole + 1);
    const uint32_t duty = (uint32_t)lroundf(dutyLow + (speed - (float)whole) * (dutyHigh - dutyLow));
    setMotorDutyRaw(duty);
    motorDutyApplied = (uint16_t)duty;
}

// The sweep blocks the control task, so it keeps the 100 ms measurements going
// itself: the rail over-voltage check and the tacho (and the UI read-outs).
static void sweepWait(uint32_t ms)
{
    static uint32_t sinceMeasure = 0;
    vTaskDelay(pdMS_TO_TICKS(ms));
    sinceMeasure += ms;
    if (sinceMeasure >= CONTROL_PERIOD_MS)
    {
        sinceMeasure = 0;
        if (boardHasVoltageControl)
            updateMotorRail();
        updateMeasuredFreq();
    }
}

// One leg, up (climb) or down (the climb backwards). needleSweepSpeed is read
// live, so moving the slider retimes a sweep in progress.
static void sweepLeg(bool up, uint16_t lowSpeed, uint16_t topSpeed)
{
    const float travel = (float)(topSpeed - lowSpeed);
    const float rate = 1.0f / (1.0f - SWEEP_EASE / 2.0f);
    float t = 0.0f;
    while (t < 1.0f)
    {
        const float pace = needleSweepSpeed < 1 ? 1.0f : (float)needleSweepSpeed;
        t += (float)SWEEP_TICK_MS / (rate * travel * pace); // steady part = 1 dial unit per `pace` ms
        if (t > 1.0f)
            t = 1.0f;
        sweepOutput((float)lowSpeed + travel * sweepProfile(up ? t : 1.0f - t));
        sweepWait(SWEEP_TICK_MS);
    }
}

// At the top: wait until the needle is really there before turning round. With a
// tacho, until the motor reaches the top mark's speed; without, a fixed hold.
static void sweepHoldAtTop(uint16_t topSpeed)
{
    const bool tacho = boardHasVoltageControl && feedbackAvailable && driveCalHzValid();
    const float arrivedHz = tacho ? SWEEP_TOP_ARRIVED * hzForSpeed((float)topSpeed) : 0.0f;
    uint32_t waited = 0;
    while (waited < (tacho ? SWEEP_TOP_WAIT_MAX_MS : SWEEP_TOP_HOLD_MS))
    {
        sweepWait(SWEEP_TICK_MS);
        waited += SWEEP_TICK_MS;
        if (tacho && measuredFreqRawHz >= arrivedHz)
            break;
    }
    vTaskDelay(pdMS_TO_TICKS(SWEEP_TOP_DWELL_MS));
}

void needleSweep()
{
    uint16_t lowSpeed = 0, topSpeed = 0;
    if (!sweepRange(lowSpeed, topSpeed))
    {
        DEBUG_IO("needle sweep: no calibration, skipped");
        return;
    }
    DEBUG_IO("needle sweep: %u..%u%s", lowSpeed, topSpeed, clusterInMph ? " mph" : " km/h");
    sweepLeg(true, lowSpeed, topSpeed);
    sweepHoldAtTop(topSpeed);
    sweepLeg(false, lowSpeed, topSpeed);
    sweepOutput(0.0f); // cut the motor at the lowest speed: it coasts the needle home
}
