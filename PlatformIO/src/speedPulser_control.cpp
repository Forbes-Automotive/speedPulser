#include "speedPulser_control.h"
#include "speedPulser_defs.h"

// ===== Control Variables =====
volatile unsigned long dutyCycleIncoming = 0;
volatile TickType_t lastPulse = 0;   // FreeRTOS tick count — written in ISR
volatile uint16_t ledCounter = 0;

bool ledOnboard = false;
uint16_t dutyCycle = 0;
uint16_t appliedDutyCycle = 0;
uint16_t tempDutyCycle = 385;
int16_t currentSpeedOffset = 0;
uint16_t rawCount = 0;
bool testNeedleSweep = false;

RunningMedian samples = RunningMedian(10);  // fixed max size for variable averageFilter (max 10)

void normaliseSpeedOffsetCurve() {
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++) {
    speedOffsetCurveOffsets[i] = constrain(speedOffsetCurveOffsets[i], -20, 20);
  }
}

int16_t getCurveOffsetForSpeed(uint16_t speedKph) {
  if (speedKph <= 50) {
    return speedOffsetCurveOffsets[0];
  }
  if (speedKph <= 100) {
    return speedOffsetCurveOffsets[1];
  }
  if (speedKph <= 150) {
    return speedOffsetCurveOffsets[2];
  }
  if (speedKph <= 200) {
    return speedOffsetCurveOffsets[3];
  }
  return speedOffsetCurveOffsets[4];
}

uint16_t applyConfiguredSpeedOffset(uint16_t speedKph) {
  normaliseSpeedOffsetCurve();

  int32_t correctedSpeed = (int32_t)speedKph;
  int16_t offsetToApply = 0;

  if (useSpeedOffsetCurve) {
    offsetToApply = getCurveOffsetForSpeed(speedKph);
  } else {
    offsetToApply = speedOffsetPositive ? (int16_t)speedOffset : -(int16_t)speedOffset;
  }

  currentSpeedOffset = offsetToApply;
  correctedSpeed += offsetToApply;

  if (correctedSpeed < 0) {
    correctedSpeed = 0;
  }

  if (correctedSpeed > 400) {
    correctedSpeed = 400;
  }

  return (uint16_t)correctedSpeed;
}

// ===== Interrupt Handler =====
// Interrupt routine for the incoming pulse from opto-isolator
void incomingHz() {
  static unsigned long previousMicros = micros();
  unsigned long presentMicros = micros();
  unsigned long revolutionTime = presentMicros - previousMicros;
  
  if (revolutionTime < 1000UL) return;  // debounce, avoid divide by 0
  
  dutyCycleIncoming = (60000000UL / revolutionTime) / 60;  // calculate frequency
  previousMicros = presentMicros;
  lastPulse = xTaskGetTickCountFromISR();  // FreeRTOS-safe tick snapshot
  ledCounter++;  // count for LED flashing
}

// ===== Speed Matching Function =====
uint16_t findClosestMatch(uint16_t val) {
  // Find the nearest match of speed from the incoming duty
  // The 'find' function returns array position, which equals the duty cycle
  uint16_t closest = 0;
  uint16_t closest2 = 0;
  uint16_t i = 0;
  bool speedTest = false;

  for (i = 0; i < sizeof motorPerformance / sizeof motorPerformance[0]; i++) {
    if (motorPerformance[i] > 0) {
      if (abs(val) > motorPerformance[i]) {
        speedTest = true;
        i = (sizeof motorPerformance / sizeof motorPerformance[0]);
      }
    }
  }

  if (speedTest) {
    for (i = 0; i < sizeof motorPerformance / sizeof motorPerformance[0]; i++) {
      if (abs(val - closest) >= abs(val - motorPerformance[i])) {
        closest = motorPerformance[i];
      }
    }

    for (i = 0; i < sizeof motorPerformance / sizeof motorPerformance[0]; i++) {
      if (motorPerformance[i] == closest) {
        closest2 = i;
        i = (sizeof motorPerformance / sizeof motorPerformance[0]);
      }
    }

    if (closest2 >= 385) {  // safety check: if not found, return 0
      return 0;
    } else {
      return closest2;  // return the correct duty cycle
    }
  } else {
    return 0;
  }
}

// ===== Median Filter Reset =====
// Call after changing averageFilter to prevent stale sample mix
void resetMedianFilter() {
  rawCount = 0;
  samples.clear();
}

// ===== Speed Control Task =====
// FreeRTOS task for continuous speed control loop
void speedControlTask(void *parameter) {
  while (1) {
    // Flash onboard LED to show incoming pulses; rolls over at averageFilter count
    if (ledCounter >= averageFilter) {
      ledOnboard = !ledOnboard;
      digitalWrite(pinOnboardLED, ledOnboard);
      ledCounter = 0;
    }

    // Reset speed to zero if no pulses received for durationReset ms
    if ((xTaskGetTickCount() - lastPulse) > pdMS_TO_TICKS(durationReset)) {
      if (!testSpeedo && !testCal) {
        ledcWrite(LEDC_CHANNEL_MOTOR, 0);
        appliedDutyCycle = 0;
      }
      // Speed display updated via REST API
    }

    if (testNeedleSweep) {
      needleSweep();
      testNeedleSweep = false;
    }

    // Test mode: manual speed or duty cycle
    if (testSpeedo || testCal) {
      testSpeed();
    }

    // Normal operation: convert incoming pulses to motor speed
    if (!testSpeedo && !testCal) {
      if (dutyCycle != dutyCycleIncoming) {  // only update PWM if speed changed
        DEBUG_PRINTF("DutyIncomingHall: %d", dutyCycleIncoming);

        // Map incoming frequency range to speed range
        uint16_t mappedSpeed = map(dutyCycleIncoming, 0, maxFreqHall, 0, maxSpeed);
        DEBUG_PRINTF("DutyPostProc1Hall: %d", mappedSpeed);

        // Collect samples for median filtering
        if (rawCount < averageFilter) {
          samples.add(mappedSpeed);
          rawCount++;
        }

        // Once we have enough samples, calculate median and apply offset
        if (rawCount >= averageFilter) {
          dutyCycle = samples.getMedian();
          DEBUG_PRINTF("getAverageHall: %d", dutyCycle);
          
        // Speed display updated via REST API /api/status endpoint
        // Live speed value available in dutyCycle variable

          uint16_t finalDuty = applyConfiguredSpeedOffset(dutyCycle);
          finalDuty = finalDuty * speedMultiplier;
          if (convertToMPH) {
            finalDuty = finalDuty * mphFactor;
          }
          finalDuty = findClosestMatch(finalDuty);
          ledcWrite(LEDC_CHANNEL_MOTOR, finalDuty);
          appliedDutyCycle = finalDuty;

          DEBUG_PRINTF("FindClosestMatchHall: %d", finalDuty);
          rawCount = 0;
          samples.clear();
        }
      }
      dutyCycle = dutyCycleIncoming;
    }

    // Delay until next cycle
    vTaskDelay(1);
  }
}
