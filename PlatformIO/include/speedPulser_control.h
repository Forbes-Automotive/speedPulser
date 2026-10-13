#ifndef SPEED_PULSER_CONTROL_H
#define SPEED_PULSER_CONTROL_H

#include <Arduino.h>
#include <RunningMedian.h>

// ===== Control-specific includes =====
extern uint8_t averageFilter;            // sample count for median filter (1-10)
#define durationReset 1500               // duration before reset speed to zero

// ===== Motor/Speed Control Variables =====
extern volatile unsigned long dutyCycleIncoming;
extern volatile TickType_t lastPulse;   // FreeRTOS tick count — set in ISR, compared in task
extern volatile uint16_t ledCounter;
extern volatile uint32_t feedbackCount;  // feedback pulse counter — incremented in ISR

extern bool ledOnboard;
extern uint16_t dutyCycle;
extern uint16_t appliedDutyCycle;
extern uint16_t requestedSpeed;
extern uint16_t tempDutyCycle;
extern uint16_t tempSpeed;
extern int16_t currentSpeedOffset;
extern uint16_t rawCount;
extern bool testNeedleSweep;

// ===== Direction & Feedback (PID) =====
extern bool reverseDirection;
extern bool feedbackEnable;
extern float pidKp;
extern float pidKi;
extern float pidKd;
extern uint16_t feedbackMaxFreq;
extern uint16_t measuredSpeed;
extern float measuredFreqHz;             // live tacho frequency (Hz) from the last loop pass
extern float measuredFreqRawHz;          // un-smoothed tacho frequency (Hz), diagnostic
extern int16_t pidCorrection;

// ===== Speed processing =====
extern uint16_t motorPerformance[385];   // calibration array
extern RunningMedian samples;
extern uint16_t maxFreqHall;
extern uint16_t maxSpeed;
extern uint8_t speedOffset;
extern bool speedOffsetPositive;
extern bool testSpeedo;
extern bool testCal;
extern uint8_t sweepSpeed;

// ===== Function declarations =====
void incomingHz(void);                    // Interrupt handler for incoming pulses
uint16_t findClosestMatch(uint16_t val);  // Find closest speed match in calibration array
int16_t getCurveOffsetForSpeed(uint16_t speedKph); // Interpolated offset from 5-point curve
uint16_t applyConfiguredSpeedOffset(uint16_t speedKph); // Apply selected offset strategy
void resetMedianFilter();                 // Reset sample buffer (call when averageFilter changes)
void resetPid();                          // Reset PID accumulators
int16_t applyFeedbackTrim(uint16_t targetSpeed, uint16_t baseDuty); // PID duty correction
bool calibrateFeedbackMaxFreq(uint16_t actualSpeed); // legacy one-point tacho-Hz<->speed calibration
uint32_t speedToPwmDuty(uint16_t speedKph); // speed -> interpolated hardware PWM duty (finer than the cal grid)
float updateMeasuredFreq();               // measure + publish motor tacho frequency (Hz) — 100 ms cadence
float speedToTachoHz(float speed);        // tacho Hz expected at a dial speed (drive table, else legacy linear)
void syncFeedbackMaxFreq();               // re-derive feedbackMaxFreq (Hz at maxSpeed) from the drive table
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

// Voltage board: open-loop drive from the calibration table + optional slow feedback trim
uint16_t applyDriveControl(uint16_t targetSpeed); // returns the raw PWM duty applied
void resetDriveControl();                 // drop the feedback trim
extern float driveTrim;                   // feedback trim on the calibrated drive (fraction, ±vcTrimLimit)
void speedControlTask(void *parameter);   // FreeRTOS task for speed control

// ===== LEDC PWM Configuration =====
#define LEDC_CHANNEL_MOTOR    LEDC_CHANNEL_0
#define LEDC_TIMER_MOTOR      LEDC_TIMER_0
#define LEDC_MODE             LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY         10000  // Hz

// ===== V_ADJ "DAC" (buck voltage control, V4 board) =====
// A separate LEDC timer/channel drives GPIO6 as a PWM->RC analogue setpoint into
// the buck FB node. 20 kHz sits far above the FB injection RC corner (~160 Hz),
// so the residual ripple on the motor rail is negligible.
#define LEDC_CHANNEL_VADJ     LEDC_CHANNEL_1
#define LEDC_TIMER_VADJ       LEDC_TIMER_1
#define VADJ_PWM_FREQUENCY    20000  // Hz
#define VADJ_RESOLUTION       10     // bits (0..1023) — smooth enough for an analogue voltage setpoint
#define VADJ_DUTY_MAX         ((1u << VADJ_RESOLUTION) - 1)
#define PWM_RESOLUTION        12     // bits (4096 levels) — raised from 10 for finer low-speed granularity.
                                     //   10 kHz * 2^12 = 40.96 MHz, within the 80 MHz LEDC clock (valid).
                                     //   13-bit would need 81.9 MHz at 10 kHz and FAILS to configure.
#define CAL_RESOLUTION        10     // bits — resolution the calibration tables (motorPerformance[]) were captured at
#define DUTY_SCALE_SHIFT      (PWM_RESOLUTION - CAL_RESOLUTION)  // left-shift: calibration duty -> hardware PWM duty
#define PWM_DUTY_MAX          ((1u << PWM_RESOLUTION) - 1)       // full-scale hardware duty (4095 = 100% = 4.0 V)

#endif  // SPEED_PULSER_CONTROL_H
