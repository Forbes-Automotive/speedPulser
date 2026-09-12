#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"

// ---------------------------------------------------------------------------
// SpeedPulser V4 — buck voltage-control hardware layer.
// See speedPulser_voltage.h for the design overview. This file owns the board
// detection, the V_ADJ PWM "DAC" and the buck-enable pin; the mid-ranging
// control law itself lives in speedPulser_control.cpp.
// ---------------------------------------------------------------------------

// ===== Board detection + live state =====
bool  boardHasVoltageControl = false;
bool  buckEnabled = false;
float lastVoltageCmd = 0.0f;
float lastPwmFrac = 0.0f;

// ===== User configuration (defaults; overwritten from EEP) =====
bool  voltageControlEnable = true;
float vcPwmNominal = 0.70f; // keep the fast PWM loop ~70% so it has head-room both ways
float vcPwmMin     = 0.15f; // never drop the throttle below this (TY3816B min-duty floor)
float vcVoltMin    = 0.15f; // lowest usable motor-supply command
float vcVoltMax    = 1.00f; // full motor-supply command
float vcVoltGain   = 0.50f; // slow voltage integrator gain (Kv)
float vcKp = 1.50f;
float vcKi = 2.00f;
float vcKd = 0.00f;

// ===== Manual calibration control =====
float tempVoltageCmd = 0.0f;

// Read the board-version strap once at boot. The V4 board ties GPIO3 to GND via
// 1k, so with the internal pull-up it reads LOW; a legacy board has no resistor
// and floats HIGH. Sampled before GPIO3 is used for anything else.
void detectBoardVersion()
{
    pinMode(pinBoardVersion, INPUT_PULLUP);
    delayMicroseconds(50); // let the node settle through the internal pull-up
    boardHasVoltageControl = (digitalRead(pinBoardVersion) == LOW);
    DEBUG_IO("board version: GPIO%d=%s -> %s", pinBoardVersion,
             boardHasVoltageControl ? "LOW" : "HIGH",
             boardHasVoltageControl ? "V4 (buck voltage control)" : "legacy (PWM only)");
}

// Map a 0..1 motor-voltage command to the V_ADJ PWM duty. The injection into the
// TPS62933 FB node is INVERSE: full motor volts = 0 duty, minimum volts = full duty.
static uint32_t voltageCmdToVadjDuty(float cmd01)
{
    if (cmd01 < 0.0f) cmd01 = 0.0f;
    if (cmd01 > 1.0f) cmd01 = 1.0f;
    return (uint32_t)((1.0f - cmd01) * (float)VADJ_DUTY_MAX + 0.5f);
}

void setMotorVoltageCmd(float cmd01)
{
    if (!boardHasVoltageControl) return;
    if (cmd01 < vcVoltMin) cmd01 = vcVoltMin; // stay inside the configured usable window
    if (cmd01 > vcVoltMax) cmd01 = vcVoltMax;
    lastVoltageCmd = cmd01;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_VADJ, voltageCmdToVadjDuty(cmd01));
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_VADJ);
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

    // Start at MINIMUM motor voltage (safe) before the buck is enabled.
    lastVoltageCmd = vcVoltMin;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_VADJ, voltageCmdToVadjDuty(vcVoltMin));
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_VADJ);
    DEBUG_IO("voltage control ready (V_ADJ GPIO%d, EN GPIO%d) — buck OFF, V=min",
             pinVoltageAdjust, pinBuckEnable);
}

void enableBuck()
{
    if (!boardHasVoltageControl || buckEnabled) return;
    setMotorVoltageCmd(vcVoltMin);     // guarantee a safe (low) rail first
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
