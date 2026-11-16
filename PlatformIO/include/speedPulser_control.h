#ifndef SPEED_PULSER_CONTROL_H
#define SPEED_PULSER_CONTROL_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SpeedPulser — speed input -> needle target -> motor.
//
//   speed sensor pulses (GPIO5 interrupt, glitch filter)
//     -> inputFrequencyHz (averaged over inputSmoothingMs)
//     -> inputSpeedKph = Hz x 3600 / sensorPulsesPerKm (held at maxSpeed)
//     -> needleTargetSpeed = offset (global or curve), then km/h -> mph if the
//        dial reads mph (cluster units, what the needle should show)
//     -> voltage board: drive calibration (+ feedback trim) -> rail + PWM
//        legacy board:  preset / custom table -> PWM duty
//
// Speed Test feeds speedTestKph in place of the sensor; Calibration Mode sets the
// motor directly. Everything runs on one 100 ms tick in speedControlTask.
// ---------------------------------------------------------------------------

#define CONTROL_PERIOD_MS 100

// ===== Live values (for the UI and the bench) =====
extern float inputFrequencyHz;           // speed sensor frequency (Hz), averaged
extern float inputSpeedKph;              // sensor speed (km/h), before the offset and mph
extern uint16_t needleTargetSpeed;       // what the needle should show now (cluster units)
extern float speedOffsetApplied;         // offset applied to the current speed (km/h)
extern uint16_t motorDutyApplied;        // motor PWM duty written last (0..PWM_DUTY_MAX)
extern uint16_t calibrationProbeDuty;    // legacy Calibration Mode: the duty the user jogged to
extern float measuredFreqHz;             // motor tacho (Hz), smoothed
extern float measuredFreqRawHz;          // motor tacho (Hz), unsmoothed
extern float driveTrim;                  // voltage board: feedback trim on the calibrated drive (fraction)

// ===== Speed input =====
void speedInputPulse();                  // speed sensor interrupt (GPIO5)
void updateInputGlitchFilter();          // call when sensorPulsesPerKm or maxSpeed change
uint16_t toNeedleTarget(float speedKph); // km/h -> offset -> cluster units (what the needle shows)

// ===== Legacy board: speed -> PWM duty =====
uint32_t speedToPwmDuty(uint16_t speed); // preset / custom table, interpolated to the 12-bit duty

// ===== Motor tacho (voltage board) =====
void feedbackPulse();                    // tacho interrupt (GPIO4)
float updateMeasuredFreq();              // measure + publish the tacho frequency, every 100 ms
// Last ~1 s of tacho: mean, spread ((max-min)/mean, %) and drift (newer half-second
// mean minus older half-second mean, Hz).
bool feedbackHzWindow(float &meanHz, float &spreadPct, float &driftHz);
// "Steady" = the MEAN has stopped moving. Cogging at low speed is a wobble around a
// constant mean (e.g. 23.1-24.7 Hz, ~7 % p-p) and averages out of each half, so it
// shows as spread but not drift; a motor still settling after a drive change shows
// as drift. The spread limit only rejects a motor that is stalling / stuttering.
#define CAL_MAX_DRIFT_PCT   2.0f          // max |drift| as % of the mean...
#define CAL_MAX_DRIFT_HZ    0.75f         // ...or in Hz, whichever is looser (low speeds)
#define CAL_MAX_SPREAD_PCT  20.0f         // max (max-min)/mean...
#define CAL_MAX_SPREAD_HZ   4.0f          // ...or in Hz, whichever is looser
inline bool feedbackHzSteady(float meanHz, float spreadPct, float driftHz)
{
  const float spreadHz = spreadPct * meanHz / 100.0f;
  const bool driftOk = fabsf(driftHz) <= CAL_MAX_DRIFT_HZ ||
                       fabsf(driftHz) * 100.0f / meanHz <= CAL_MAX_DRIFT_PCT;
  const bool spreadOk = spreadHz <= CAL_MAX_SPREAD_HZ || spreadPct <= CAL_MAX_SPREAD_PCT;
  return driftOk && spreadOk;
}

// ===== Voltage board: drive from the calibration + feedback trim =====
uint16_t applyDriveControl(uint16_t targetSpeed); // returns the raw PWM duty applied
void resetDriveControl();                // drop the feedback trim

bool motorIsStopped();                   // no drive, no duty and no input speed
void speedControlTask(void *parameter);  // the 100 ms control task

// ===== LEDC PWM Configuration =====
#define LEDC_CHANNEL_MOTOR    LEDC_CHANNEL_0
#define LEDC_TIMER_MOTOR      LEDC_TIMER_0
#define LEDC_MODE             LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY         10000  // Hz

// ===== V_ADJ "DAC" (buck voltage control) =====
// A separate LEDC timer/channel drives GPIO6 as a PWM->RC analogue setpoint into
// the buck FB node. 20 kHz sits far above the FB injection RC corner (~160 Hz),
// so the residual ripple on the motor rail is negligible.
#define LEDC_CHANNEL_VADJ     LEDC_CHANNEL_1
#define LEDC_TIMER_VADJ       LEDC_TIMER_1
#define VADJ_PWM_FREQUENCY    20000  // Hz
#define VADJ_RESOLUTION       10     // bits (0..1023) — smooth enough for an analogue voltage setpoint
#define VADJ_DUTY_MAX         ((1u << VADJ_RESOLUTION) - 1)
#define PWM_RESOLUTION        12     // bits (4096 levels). 10 kHz * 2^12 = 40.96 MHz, within the
                                     // 80 MHz LEDC clock; 13-bit would need 81.9 MHz and fails.
#define CAL_RESOLUTION        10     // bits — resolution the preset tables were captured at
#define DUTY_SCALE_SHIFT      (PWM_RESOLUTION - CAL_RESOLUTION)  // preset duty -> hardware duty
#define PWM_DUTY_MAX          ((1u << PWM_RESOLUTION) - 1)       // 4095 = 100 %

#endif  // SPEED_PULSER_CONTROL_H
