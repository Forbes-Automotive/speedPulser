#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_tasks.h"

void basicInit()
{
    DEBUG_PRINTLN("Initialising SpeedPulser...");

    DEBUG_PRINTLN("Setting up LED Output...");
    pinMode(pinOnboardLED, OUTPUT);
    digitalWrite(pinOnboardLED, ledOnboard);
    DEBUG_PRINTLN("Set up LED Output!");

    DEBUG_PRINTLN("Setting up LEDC PWM...");
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
    DEBUG_PRINTLN("Set up LEDC PWM!");

    DEBUG_PRINTLN("Setting up Speed Interrupt...");
    attachInterrupt(digitalPinToInterrupt(pinSpeedInput), incomingHz, FALLING);
    DEBUG_PRINTLN("Set up speed interrupt!");

    DEBUG_PRINTLN("Initialised SpeedPulser!");
}

void testSpeed()
{
    // check to see if tempSpeed has a value.  IF it does (>0), set the speed using the 'find closest match' as a duty cycle
    if (testCal)
    {
        ledcWrite(LEDC_CHANNEL_MOTOR, tempDutyCycle);
        appliedDutyCycle = tempDutyCycle;
#if serialDebug
        DEBUG_PRINTF("     Duty: %d", tempDutyCycle);
#endif
    }

    if (!testCal && tempSpeed > 0)
    {
#if serialDebug
        DEBUG_PRINTF("Chosen Speed: %d", tempSpeed);
#endif
        dutyCycle = applyConfiguredSpeedOffset(tempSpeed);
        if (dutyCycle > 0)
        {
            dutyCycle = dutyCycle * speedMultiplier;
            if (convertToMPH)
            {
                dutyCycle = dutyCycle * mphFactor;
            }
            dutyCycle = findClosestMatch(dutyCycle);
            ledcWrite(LEDC_CHANNEL_MOTOR, dutyCycle);
            appliedDutyCycle = dutyCycle;
        }
        else
        {
            ledcWrite(LEDC_CHANNEL_MOTOR, 0);
            appliedDutyCycle = 0;
        }
#if serialDebug
        DEBUG_PRINTF("  Final Duty: %d", dutyCycle);
        DEBUG_PRINTLN("");
#endif
    }
}

void needleSweep()
{
    // Maximum raw duty used by the calibration table
    const uint16_t kMaxDuty = (sizeof motorPerformance / sizeof motorPerformance[0]) - 1;  // 384
    // Total ramp duration: preserve the same feel as the old per-step timing
    const uint32_t kFadeMs  = (uint32_t)sweepSpeed * kMaxDuty;

    // Ramp up — hardware linear interpolation, no step jitter
    ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_MOTOR, kMaxDuty, kFadeMs);
    ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_MOTOR, LEDC_FADE_WAIT_DONE);  // yields CPU via semaphore (FreeRTOS-safe)

    // Pause at full deflection
    vTaskDelay(pdMS_TO_TICKS((uint32_t)sweepSpeed * 2));

    // Ramp back down — hardware linear interpolation
    ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_MOTOR, 0, kFadeMs);
    ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_MOTOR, LEDC_FADE_WAIT_DONE);  // yields CPU via semaphore (FreeRTOS-safe)

    vTaskDelay(pdMS_TO_TICKS((uint32_t)sweepSpeed * 2));
    dutyCycle = 0;
    appliedDutyCycle = 0;
    ledcWrite(LEDC_CHANNEL_MOTOR, 0);  // ensure output is fully off after sweep
}
