#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_tasks.h"

void basicInit()
{
    DEBUG_IO("initialising SpeedPulser I/O...");

    DEBUG_IO("setting up LED output...");
    pinMode(pinOnboardLED, OUTPUT);
    digitalWrite(pinOnboardLED, ledOnboard);
    DEBUG_IO("LED output ready (GPIO%d)", pinOnboardLED);

    DEBUG_IO("setting up LEDC PWM...");
    // Setup LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)PWM_RESOLUTION,
        .timer_num = LEDC_TIMER_MOTOR,
        .freq_hz = PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    // Setup LEDC channel
    ledc_channel_config_t ledc_channel = {
        .gpio_num = pinMotorOutput,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL_MOTOR,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_MOTOR,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);
    ledc_fade_func_install(0);  // install LEDC hardware fade ISR (required once before any fade calls)
    DEBUG_IO("LEDC PWM ready (GPIO%d, %d-bit @ %d Hz)", pinMotorOutput, PWM_RESOLUTION, PWM_FREQUENCY);

    DEBUG_IO("setting up speed interrupt...");
    attachInterrupt(digitalPinToInterrupt(pinSpeedInput), incomingHz, FALLING);
    DEBUG_IO("speed interrupt ready (GPIO%d, FALLING)", pinSpeedInput);

    DEBUG_IO("setting up direction output...");
    pinMode(pinDirection, OUTPUT);
    applyDirection();  // normal = LOW, reverse = HIGH
    DEBUG_IO("direction output ready (GPIO%d, %s)", pinDirection, reverseDirection ? "REV" : "FWD");

    DEBUG_IO("setting up feedback input (GPIO%d)...", pinFeedback);
    pinMode(pinFeedback, INPUT_PULLUP);  // C3 has no PCNT; count pulses via ISR
    attachInterrupt(digitalPinToInterrupt(pinFeedback), feedbackPulse, FALLING);
    DEBUG_IO("feedback input ready (GPIO%d, FALLING, pull-up)", pinFeedback);

    DEBUG_IO("SpeedPulser I/O initialised!");
}

// Drive the direction pin from the reverseDirection flag.
// Default (unticked) idles LOW = normal direction; ticking Reverse pulls HIGH.
void applyDirection()
{
    digitalWrite(pinDirection, reverseDirection ? HIGH : LOW);
}

// Write the motor PWM duty via the ESP-IDF LEDC driver. The channel is set up
// with ledc_channel_config() above, so duty must be written the IDF way too.
// (Arduino-ESP32 3.x made ledcWrite() pin-based and it no longer recognises
// channels created outside its own ledcAttach() bookkeeping.)
//
// setMotorDuty() takes a duty in the original 10-bit calibration domain (0..384)
// — the domain every calibration table, the PID and the cal UI work in — and
// scales it up to the PWM_RESOLUTION-bit hardware range. This keeps every stored
// calibration voltage-identical while the hardware gains DUTY_SCALE_SHIFT extra
// bits of resolution.
void setMotorDuty(uint32_t duty)
{
    setMotorDutyRaw(duty << DUTY_SCALE_SHIFT);
}

// Write a duty straight to the PWM hardware domain (0..(1<<PWM_RESOLUTION)-1).
// Used by the interpolated speed path and the needle sweep, which compute
// sub-count precision that would be lost if forced back onto the 10-bit grid.
void setMotorDutyRaw(uint32_t pwmDuty)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_MOTOR, pwmDuty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_MOTOR);
}

void testSpeed()
{
    // check to see if tempSpeed has a value.  IF it does (>0), set the speed using the 'find closest match' as a duty cycle
    if (testCal)
    {
        // Calibration probe: drive the raw 12-bit hardware duty directly so every
        // single count (0..PWM_DUTY_MAX) is reachable — lets you hunt the true motor
        // start threshold without the x4 quantisation of the calibration grid.
        setMotorDutyRaw(tempDutyCycle);
        appliedDutyCycle = tempDutyCycle;
        DEBUG_CTRL("test-cal: raw pwm=%u/%u", tempDutyCycle, PWM_DUTY_MAX);
    }

    if (!testCal && tempSpeed > 0)
    {
        DEBUG_CTRL("test-speed: chosen=%u kph", tempSpeed);
        uint16_t spd = applyConfiguredSpeedOffset(tempSpeed);
        if (spd > 0)
        {
            spd = spd * speedMultiplier;
            if (convertToMPH)
            {
                spd = spd * mphFactor;
            }
            dutyCycle = findClosestMatch(spd);       // cal-domain duty (for legacy display)
            uint32_t hwDuty = speedToPwmDuty(spd);    // 12-bit hardware duty actually applied
            setMotorDutyRaw(hwDuty);
            appliedDutyCycle = (uint16_t)hwDuty;      // report the real applied duty (matches the curve)
        }
        else
        {
            setMotorDuty(0);
            appliedDutyCycle = 0;
        }
        DEBUG_CTRL("test-speed: final duty=%u", dutyCycle);
    }
}

void needleSweep()
{
    // Startup needle exercise: drive the gauge to its MECHANICAL full deflection
    // and back. This is a physical self-test, not a speed readout, so it ramps the
    // whole 12-bit PWM range (0..PWM_DUTY_MAX). The 10-bit presets only reach
    // 384<<DUTY_SCALE_SHIFT (~37% of range), which left the needle well short of
    // the peg on the 12-bit hardware.
    const uint32_t kPollMs  = 10;                               // software-fade cadence
    const long     kSpan    = (maxSpeed < 10) ? 200 : maxSpeed; // sets sweep DURATION only
    const uint32_t kMaxDuty = PWM_DUTY_MAX;                     // full deflection = full 12-bit duty

    // Ramp UP. Progress is INTEGRATED per tick from the LIVE sweepSpeed, so
    // dragging the Sweep Speed slider retimes an in-progress sweep smoothly.
    float progress = 0.0f;
    while (progress < 1.0f)
    {
        uint32_t fullMs = (uint32_t)sweepSpeed * (uint32_t)kSpan;  // time for a full 0..max sweep
        if (fullMs < 1) fullMs = 1;
        progress += (float)kPollMs / (float)fullMs;
        if (progress > 1.0f) progress = 1.0f;
        setMotorDutyRaw((uint32_t)(kMaxDuty * progress));
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
    setMotorDutyRaw(kMaxDuty);                                    // pin to full deflection

    // Pause at full deflection
    vTaskDelay(pdMS_TO_TICKS((uint32_t)sweepSpeed * 2));

    // Ramp DOWN — mirror of the ramp-up integration
    progress = 0.0f;
    while (progress < 1.0f)
    {
        uint32_t fullMs = (uint32_t)sweepSpeed * (uint32_t)kSpan;
        if (fullMs < 1) fullMs = 1;
        progress += (float)kPollMs / (float)fullMs;
        if (progress > 1.0f) progress = 1.0f;
        setMotorDutyRaw((uint32_t)(kMaxDuty * (1.0f - progress)));
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }

    vTaskDelay(pdMS_TO_TICKS((uint32_t)sweepSpeed * 2));
    dutyCycle = 0;
    appliedDutyCycle = 0;
    setMotorDuty(0);  // ensure output is fully off after sweep
}
