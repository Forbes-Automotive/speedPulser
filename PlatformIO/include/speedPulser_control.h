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
bool calibrateFeedbackMaxFreq(uint16_t actualSpeed); // one-point tacho-Hz<->speed calibration
uint32_t speedToPwmDuty(uint16_t speedKph); // speed -> interpolated hardware PWM duty (finer than the cal grid)
void speedControlTask(void *parameter);   // FreeRTOS task for speed control

// ===== LEDC PWM Configuration =====
#define LEDC_CHANNEL_MOTOR    LEDC_CHANNEL_0
#define LEDC_TIMER_MOTOR      LEDC_TIMER_0
#define LEDC_MODE             LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY         10000  // Hz
#define PWM_RESOLUTION        12     // bits (4096 levels) — raised from 10 for finer low-speed granularity.
                                     //   10 kHz * 2^12 = 40.96 MHz, within the 80 MHz LEDC clock (valid).
                                     //   13-bit would need 81.9 MHz at 10 kHz and FAILS to configure.
#define CAL_RESOLUTION        10     // bits — resolution the calibration tables (motorPerformance[]) were captured at
#define DUTY_SCALE_SHIFT      (PWM_RESOLUTION - CAL_RESOLUTION)  // left-shift: calibration duty -> hardware PWM duty
#define PWM_DUTY_MAX          ((1u << PWM_RESOLUTION) - 1)       // full-scale hardware duty (4095 = 100% = 4.0 V)

#endif  // SPEED_PULSER_CONTROL_H
