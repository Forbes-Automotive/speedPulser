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

extern bool ledOnboard;
extern uint16_t dutyCycle;
extern uint16_t appliedDutyCycle;
extern uint16_t tempDutyCycle;
extern uint16_t tempSpeed;
extern int16_t currentSpeedOffset;
extern uint16_t rawCount;
extern bool testNeedleSweep;

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
void speedControlTask(void *parameter);   // FreeRTOS task for speed control

// ===== LEDC PWM Configuration =====
#define LEDC_CHANNEL_MOTOR    LEDC_CHANNEL_0
#define LEDC_TIMER_MOTOR      LEDC_TIMER_0
#define LEDC_MODE             LEDC_LOW_SPEED_MODE
#define PWM_FREQUENCY         10000  // Hz
#define PWM_RESOLUTION        10     // bits (1024 levels)

#endif  // SPEED_PULSER_CONTROL_H
