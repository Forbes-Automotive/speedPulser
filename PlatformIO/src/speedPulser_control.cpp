#include "speedPulser_control.h"
#include "speedPulser_defs.h"
#include "speedPulser_calBuilder.h"

// ===== Control Variables =====
volatile unsigned long dutyCycleIncoming = 0;
volatile TickType_t lastPulse = 0;   // FreeRTOS tick count — written in ISR
volatile uint16_t ledCounter = 0;    // counts incoming pulses for onboard LED flash
volatile uint32_t feedbackCount = 0; // feedback pulses — incremented in ISR

bool ledOnboard = false;
uint16_t dutyCycle = 0;
uint16_t appliedDutyCycle = 0;
uint16_t requestedSpeed = 0; // mapped/offset road speed (kph) that generates the applied duty
uint16_t tempDutyCycle = 0;  // calibration probe value, raw 12-bit hardware duty (0..PWM_DUTY_MAX)
int16_t currentSpeedOffset = 0;
uint16_t rawCount = 0;
bool testNeedleSweep = false;

RunningMedian samples = RunningMedian(10); // fixed max size for variable averageFilter (max 10)

void normaliseSpeedOffsetCurve()
{
  for (uint8_t i = 0; i < SPEED_OFFSET_CURVE_POINTS; i++)
  {
    speedOffsetCurveOffsets[i] = constrain(speedOffsetCurveOffsets[i], -20, 20);
  }
}

int16_t getCurveOffsetForSpeed(uint16_t speedKph)
{
  if (speedKph <= 50)
  {
    return speedOffsetCurveOffsets[0];
  }
  if (speedKph <= 100)
  {
    return speedOffsetCurveOffsets[1];
  }
  if (speedKph <= 150)
  {
    return speedOffsetCurveOffsets[2];
  }
  if (speedKph <= 200)
  {
    return speedOffsetCurveOffsets[3];
  }
  return speedOffsetCurveOffsets[4];
}

uint16_t applyConfiguredSpeedOffset(uint16_t speedKph)
{
  normaliseSpeedOffsetCurve();

  int32_t correctedSpeed = (int32_t)speedKph;
  int16_t offsetToApply = 0;

  if (useSpeedOffsetCurve)
  {
    offsetToApply = getCurveOffsetForSpeed(speedKph);
  }
  else
  {
    offsetToApply = speedOffsetPositive ? (int16_t)speedOffset : -(int16_t)speedOffset;
  }

  currentSpeedOffset = offsetToApply;
  correctedSpeed += offsetToApply;

  if (correctedSpeed < 0)
  {
    correctedSpeed = 0;
  }

  if (correctedSpeed > 400)
  {
    correctedSpeed = 400;
  }

  return (uint16_t)correctedSpeed;
}

// ===== Interrupt Handler =====
// File-scope (not a function-local static): a function-local static with a
// runtime initializer would emit a __cxa_guard_acquire on first use, which takes
// a FreeRTOS mutex with a timeout — illegal in an ISR and asserts in queue.c.
static volatile unsigned long incomingPreviousMicros = 0;

// Interrupt routine for the incoming pulse from opto-isolator.
// IRAM_ATTR is required: this fires from a GPIO interrupt and must be able to
// run while the flash cache is disabled (e.g. during an EEPROM/LittleFS write).
void IRAM_ATTR incomingHz()
{
  // Ignore the vehicle hall input entirely while bench-testing or calibrating —
  // the motor is driven from the Speed Test / Cal controls, so an incoming signal
  // must not touch dutyCycleIncoming, lastPulse or the LED counter until the test
  // (or cal) is turned off.
  if (testSpeedo || testCal)
    return;

  unsigned long presentMicros = micros();
  unsigned long previousMicros = incomingPreviousMicros;

  if (previousMicros == 0)
  {
    incomingPreviousMicros = presentMicros; // seed on the first pulse only
    return;
  }

  unsigned long revolutionTime = presentMicros - previousMicros;

  if (revolutionTime < 1000UL)
    return; // debounce, avoid divide by 0

  dutyCycleIncoming = (60000000UL / revolutionTime) / 60; // calculate frequency
  incomingPreviousMicros = presentMicros;
  lastPulse = xTaskGetTickCountFromISR(); // FreeRTOS-safe tick snapshot
  ledCounter++;                           // count for LED flashing
}

// ===== Speed Matching Function =====
// motorPerformance[] maps duty index (0..384) -> speed (kph). Leading entries are
// 0 (motor won't turn) and the remainder are increasing. Return the duty whose
// calibrated speed is nearest to `val`; on an equal-distance tie prefer the lower
// duty.
uint16_t findClosestMatch(uint16_t val)
{
  const uint16_t n = sizeof motorPerformance / sizeof motorPerformance[0];

  uint16_t bestIdx = 0;
  uint32_t bestErr = UINT32_MAX;
  uint16_t minSpeed = 0;
  bool haveCal = false;

  for (uint16_t i = 0; i < n; i++)
  {
    const uint16_t spd = motorPerformance[i];
    if (spd == 0)
    {
      continue; // skip dead-zone / uncalibrated entries
    }
    if (!haveCal)
    { // first non-zero entry = lowest calibrated speed
      minSpeed = spd;
      haveCal = true;
    }

    const uint32_t err = (val > spd) ? (uint32_t)(val - spd) : (uint32_t)(spd - val);
    if (err < bestErr)
    { // strict '<' keeps the lower duty on ties
      bestErr = err;
      bestIdx = i;
    }
  }

  // Dead-zone: no calibration, or requested speed at/below the lowest achievable
  // speed -> motor off (preserves the legacy start-threshold behaviour).
  if (!haveCal || val <= minSpeed)
  {
    return 0;
  }
  return bestIdx;
}

// ===== Median Filter Reset =====
// Call after changing averageFilter to prevent stale sample mix
void resetMedianFilter()
{
  rawCount = 0;
  samples.clear();
}

// ===== Speed -> PWM duty (interpolated) =====
// findClosestMatch() snaps a requested speed onto the nearest 10-bit calibration
// duty (0..384). Since PWM hardware runs at PWM_RESOLUTION bits (currently 12-bit),
// fix on that nearest calibration point, then linearly interpolate
// toward the neighbouring point that brackets the requested speed.
// The result is a hardware duty (already scaled by DUTY_SCALE_SHIFT) with sub-count
// precision — this is what actually turns the extra PWM bits into finer control
// particularly for better low speed and mid-range control
//
// It refines the legacy (10-bit) behaviour: it returns the same speed point when
// there is nothing sensible to interpolate against (dead-zone, table ends, flat
// or zero neighbours), so calibrations and the motor start threshold are unchanged.
uint32_t speedToPwmDuty(uint16_t speedKph)
{
  // Custom (SpeedPulser-V2) calibration: interpolate straight from the speed
  // points so we use the full 12-bit duty range. The 385-entry table can only
  // represent duty up to 384<<DUTY_SCALE_SHIFT (~37.5%), which would otherwise
  // clamp a full-range custom calibration short of its top speed.
  if (motorPerformanceVal == CUSTOM_CAL_ID && customCalValid)
  {
    return customSpeedToDuty12(speedKph);
  }

  const int32_t maxIdx = (int32_t)(sizeof motorPerformance / sizeof motorPerformance[0]) - 1; // 384

  const uint16_t dNear = findClosestMatch(speedKph); // legacy nearest cal duty (handles dead-zone + noise)
  if (dNear == 0)
  {
    return 0; // below the motor's start threshold -> off
  }

  const int32_t vNear = (int32_t)motorPerformance[dNear];
  const uint32_t base = (uint32_t)dNear << DUTY_SCALE_SHIFT;
  if (vNear == (int32_t)speedKph)
  {
    return base; // exact hit — nothing to interpolate
  }

  // Step to the neighbouring calibration point on the side of the requested speed,
  // skipping flat entries so we interpolate across a real slope.
  const int32_t dir = (speedKph > vNear) ? +1 : -1;
  int32_t dOther = (int32_t)dNear + dir;
  while (dOther >= 0 && dOther <= maxIdx && motorPerformance[dOther] == (uint16_t)vNear)
  {
    dOther += dir;
  }
  if (dOther < 0 || dOther > maxIdx || motorPerformance[dOther] == 0)
  {
    return base; // no usable neighbour -> fall back to the anchor
  }

  const int32_t vOther = (int32_t)motorPerformance[dOther];
  const int32_t den = vOther - vNear; // speed slope across the interval
  if (den == 0)
  {
    return base;
  }

  // fracDuty = dNear + (speed - vNear)/(vOther - vNear) * (dOther - dNear), in PWM units
  const int32_t num = ((int32_t)speedKph - vNear) * (dOther - (int32_t)dNear);
  int32_t pwm = (int32_t)base + ((num << DUTY_SCALE_SHIFT) / den);

  const int32_t maxPwm = maxIdx << DUTY_SCALE_SHIFT;
  if (pwm < 0)
    pwm = 0;
  if (pwm > maxPwm)
    pwm = maxPwm;
  return (uint32_t)pwm;
}

// ===== Feedback Pulse Counter (period-based) =====
// ESP32-C3 has no PCNT peripheral, so the motor feedback on GPIO4 is measured
// in software via this ISR (mirrors the GPIO5 speed input pattern).
//
// Rather than only counting edges per fixed window (which quantises frequency
// to 1/window Hz, e.g. 10 Hz steps at 100 ms and so cannot resolve a half-pulse
// target), we also accumulate the exact micros() interval between edges. The
// control loop then computes freq = edges / (summed interval time), which uses
// fractional timing for continuous resolution and averages out per-commutation
// hall jitter over the window.
//
// IRAM_ATTR is required: without it, a feedback pulse arriving while the flash
// cache is disabled (EEPROM/LittleFS write) causes a crash
static volatile uint32_t feedbackAccumUs = 0;    // summed edge-to-edge intervals (us)
static volatile uint32_t feedbackLastEdgeUs = 0; // micros() of the previous edge
void IRAM_ATTR feedbackPulse()
{
  uint32_t now = micros();
  uint32_t last = feedbackLastEdgeUs;
  feedbackLastEdgeUs = now;
  if (last != 0)
  {
    feedbackAccumUs += (now - last); // interval preceding this edge
  }
  feedbackCount++;
}

// ===== PID State =====
float measuredFreqHz = 0.0f;      // motor speed (Hz), for calibration/UI
float measuredFreqRawHz = 0.0f;   // un-smoothed motor frequency (Hz) — diagnostic for the non-linear signal
static float pidIntegral = 0.0f;  // integral term
static float pidPrevError = 0.0f; // previous error
static float measFreqFilt = 0.0f; // Smoothed motor frequency (anti-jitter)

// Guards the read-and-clear of feedbackCount against the GPIO4 ISR. A portMUX
// critical section is the FreeRTOS/ESP32-safe primitive here — the old
// noInterrupts()/interrupts() pair disabled ALL interrupts globally, which can
// starve the WiFi radio and trigger a reset while feedback is enabled.
static portMUX_TYPE feedbackMux = portMUX_INITIALIZER_UNLOCKED;

void resetPid()
{
  pidIntegral = 0.0f;
  pidPrevError = 0.0f;
  measFreqFilt = 0.0f;
  pidCorrection = 0;
}

// ===== Feedback Calibration =====
// Run the motor, reads the REAL speed the motor shows, and parses it here.
// Used to trim motor speed if the motor is binding slightly - to get the best speed response
// Returns false if there's no motion to sample.
bool calibrateFeedbackMaxFreq(uint16_t actualSpeed)
{
  if (actualSpeed == 0 || measuredFreqHz < 1.0f)
    return false;
  const uint16_t spdSpan = maxSpeed > 0 ? maxSpeed : 1;
  long hz = lroundf(measuredFreqHz * (float)spdSpan / (float)actualSpeed);
  hz = constrain(hz, 1L, 65535L);
  feedbackMaxFreq = (uint16_t)hz;
  resetPid(); // scale changed -> drop stale integral so it re-settles cleanly
  return true;
}

// ===== Motor Speed Measurement =====
// Period-based motor frequency (Hz) from the GPIO4 ISR accumulators, smoothed,
// published to measuredFreqHz/measuredSpeed. Split out of the PID so the speed
// readout and calibration stay live even in open-loop / cal-builder
// modes where the PID isn't running. Call on a ~100 ms cadence.
float updateMeasuredFreq()
{
  const uint32_t FB_STALL_US = 300000UL; // no edge for 300 ms: assume stopped
  const float FB_MEAS_ALPHA = 0.4f;      // EMA weight for the measured-Hz low-pass

  portENTER_CRITICAL(&feedbackMux);
  uint32_t pulses = feedbackCount;
  uint32_t accumUs = feedbackAccumUs;
  uint32_t lastEdge = feedbackLastEdgeUs;
  feedbackCount = 0;
  feedbackAccumUs = 0;
  portEXIT_CRITICAL(&feedbackMux);

  const uint16_t scaleFreq = feedbackMaxFreq > 0 ? feedbackMaxFreq : 1;
  const uint16_t spdSpan = maxSpeed > 0 ? maxSpeed : 1;

  float measuredFreq;
  if (pulses >= 2 && accumUs > 0 &&
      (uint32_t)(micros() - lastEdge) < FB_STALL_US)
  {
    measuredFreq = (float)pulses * 1000000.0f / (float)accumUs;
  }
  else
  {
    measuredFreq = 0.0f;
  }
  measuredFreqRawHz = measuredFreq; // send raw (pre-smoothing) rate for signal diagnostics

  if (measuredFreq > 0.0f)
  {
    feedbackAvailable = true; // a genuine tacho edge stream proves the feedback pin is wired
  }

  if (measuredFreq <= 0.0f)
  {
    measFreqFilt = 0.0f;
  }
  else
  {
    measFreqFilt += FB_MEAS_ALPHA * (measuredFreq - measFreqFilt);
  }
  measuredFreqHz = measFreqFilt;                                                  // filtered freq for calibration/UI
  measuredSpeed = (uint16_t)((measFreqFilt * (float)spdSpan) / (float)scaleFreq); // kph, for UI/status only
  return measFreqFilt;
}

// ===== Feedback Trim (PID) =====
// Trims the feed-forward duty so the measured motor speed tracks the requested
// speed.
int16_t applyFeedbackTrim(uint16_t targetSpeed, uint16_t baseDuty)
{
  const float PID_PERIOD_S = 0.1f;   // 100 ms control interval
  const float deadbandHz = feedbackDeadband > 0.0f ? feedbackDeadband : 0.0f; // user-set anti-hunt band; 0 = off

  // Measure + publish the tacho (shared with the open-loop / cal-builder readout).
  const float measFreq = updateMeasuredFreq();

  // Work in the Motor FREQUENCY domain (Hz), not integer kph. Converting the
  // feedback to whole-kph throws away most of the low/mid-range resolution and
  // makes the error jump in coarse steps; Hz keeps it smooth.
  const uint16_t scaleFreq = feedbackMaxFreq > 0 ? feedbackMaxFreq : 1;
  const uint16_t spdSpan = maxSpeed > 0 ? maxSpeed : 1;
  const float targetFreq = (float)targetSpeed * (float)scaleFreq / (float)spdSpan; // Hz expected at the requested speed

  // Low-speed open-loop cutoff: below feedbackMinSpeed the motor can't run smoothly
  // so don't use PID. feedbackMinSpeed=0 disables.
  if (feedbackMinSpeed > 0 && targetSpeed < feedbackMinSpeed)
  {
    pidIntegral = 0.0f;
    pidPrevError = 0.0f;
    pidCorrection = 0;
    return (int16_t)constrain((int32_t)baseDuty, 0, (int32_t)PWM_DUTY_MAX);
  }

  const float error = targetFreq - measFreq; // Hz

  // dutyPerHz self-normalises the output: full 12-bit duty spans the full motor
  // range, so the 0..5 UI PID settings work above feedbackMaxFreq.
  // This is what gives the PID the 'muscle' to pull speed back up under load — the
  // legacy 10-bit "x4" scaling was far too weak to move the duty meaningfully.
  const float dutyPerHz = (float)PWM_DUTY_MAX / (float)scaleFreq;

  // Deadband (anti-hunt): near the motor's break-free duty the response is sticky,
  // and chasing measurement jitter sets up a limit-cycle hunt. Inside the band we
  // silence P and D so they don't react to that jitter — but the INTEGRAL keeps
  // accumulating so the loop still trims out the last couple of Hz of steady-state
  // error and actually settles on the target (freezing it here used to leave a fixed
  // ~2 km/h offset). Outside the band the full PID runs as before.
  const bool inDeadband = fabsf(error) < deadbandHz;

  pidIntegral += error * PID_PERIOD_S;
  const float maxIntegral = (float)scaleFreq / (pidKi > 0.001f ? pidKi : 0.001f); // cap iTerm's own share at full authority
  pidIntegral = constrain(pidIntegral, -maxIntegral, maxIntegral);

  const float derivative = (error - pidPrevError) / PID_PERIOD_S;
  pidPrevError = error;

  const float pTerm = inDeadband ? 0.0f : pidKp * error;
  const float iTerm = pidKi * pidIntegral; // always active — nulls the steady-state offset
  const float dTerm = inDeadband ? 0.0f : pidKd * derivative;
  const float output = (pTerm + iTerm + dTerm) * dutyPerHz;
  pidCorrection = (int16_t)constrain(output, -(float)PWM_DUTY_MAX, (float)PWM_DUTY_MAX);

  int32_t corrected = (int32_t)baseDuty + pidCorrection;
  corrected = constrain(corrected, 0, (int32_t)PWM_DUTY_MAX);

  // Confirm the motor reacts to load - brake the motor by hand and:
  // measF should drop, error should rise, and corr/applied should climb to compensate
  static uint8_t fbLogDiv = 0;
  if (++fbLogDiv >= 10)
  {
    fbLogDiv = 0;

    // [FB] diagnostics - print at ~1 Hz
    DEBUG_FB("tgtF=%.1f measF=%.1f(raw %.1f)/%u Hz | meas=%u kph err=%+.1f | P=%.0f I=%.0f D=%.0f -> corr=%+d | base=%u applied=%d/%u",
             targetFreq, measFreq, measuredFreqRawHz, scaleFreq,
             measuredSpeed, error,
             pTerm * dutyPerHz, iTerm * dutyPerHz, dTerm * dutyPerHz, pidCorrection,
             baseDuty, (int)corrected, (unsigned)PWM_DUTY_MAX);
  }

  return (int16_t)corrected; // final applied duty (0..PWM_DUTY_MAX)
}

// ===== Speed Control Task =====
// FreeRTOS task for continuous speed control loop
void speedControlTask(void *parameter)
{
  static uint16_t feedforwardDuty = 0; // calibrated base duty (PID feeds forward from this)
  static TickType_t lastPidTick = 0;
  static TickType_t lastMeasTick = 0;
  static bool signalPresent = false; // edge-detect input pulse loss/restore for [CTRL] log
  static TickType_t motorRunSince = 0; // when the motor started running with no feedback yet
  while (1)
  {
    // PID may only run when feedback is actually available. If the user enables it
    // but the PCB has no feedback trace (legacy board) the loop stays open-loop, so
    // an unaware user can't accidentally drive a closed loop with no measurement.
    const bool pidActive = feedbackEnable && feedbackAvailable;

    // Flash onboard LED to show incoming pulses; rolls over at averageFilter count
    if (ledCounter >= averageFilter)
    {
      ledOnboard = !ledOnboard;
      digitalWrite(pinOnboardLED, ledOnboard);
      ledCounter = 0;
    }

    // Reset speed to zero if no pulses received for durationReset ms
    if ((xTaskGetTickCount() - lastPulse) > pdMS_TO_TICKS(durationReset))
    {
      if (signalPresent && !testSpeedo && !testCal)
      {
        DEBUG_CTRL("input signal lost (no pulses for %lu ms) — motor off",
                   (unsigned long)durationReset);
        signalPresent = false;
      }
      dutyCycleIncoming = 0;
      dutyCycle = 0;
      rawCount = 0;
      samples.clear();

      if (!testSpeedo && !testCal)
      {
        setMotorDuty(0);
        appliedDutyCycle = 0;
        feedforwardDuty = 0;
        requestedSpeed = 0;
        measuredSpeed = 0;
        resetPid();
      }
      // Speed display updated via API
    }
    else if (!signalPresent && dutyCycleIncoming > 0 && !testSpeedo && !testCal)
    {
      DEBUG_CTRL("input signal restored (%lu Hz)", (unsigned long)dutyCycleIncoming);
      signalPresent = true;
    }

    if (testNeedleSweep)
    {
      needleSweep();
      testNeedleSweep = false;
    }

    // Test mode: manual speed or duty cycle
    if (testCal)
    {
      // Cal building is open-loop increase by default (no PID).
      // With feedback (PID) enabled it instead drives to the Speed Test target, so a
      // finished cal can be verified closed-loop without leaving the builder.
      if (pidActive)
      {
        if ((xTaskGetTickCount() - lastPidTick) >= pdMS_TO_TICKS(100))
        {
          lastPidTick = xTaskGetTickCount();
          uint16_t setpoint = applyConfiguredSpeedOffset(tempSpeed);
          setpoint = setpoint * speedMultiplier;
          if (convertToMPH)
          {
            setpoint = setpoint * mphFactor;
          }
          requestedSpeed = setpoint;
          feedforwardDuty = speedToPwmDuty(setpoint);
          uint16_t trimmed = (uint16_t)applyFeedbackTrim(setpoint, feedforwardDuty);
          setMotorDutyRaw(trimmed);
          appliedDutyCycle = trimmed;
        }
      }
      else
      {
        testSpeed(); // raw calibration probe (open-loop increase)
      }
    }
    else if (testSpeedo)
    {
      if (pidActive)
      {
        // Closed-loop bench test: hold the set speed and let the PID trim the
        // duty from the GPIO4 feedback, so you can confirm it fights a load
        // without needing a live speed input
        if ((xTaskGetTickCount() - lastPidTick) >= pdMS_TO_TICKS(100))
        {
          lastPidTick = xTaskGetTickCount();
          uint16_t setpoint = applyConfiguredSpeedOffset(tempSpeed);
          setpoint = setpoint * speedMultiplier;
          if (convertToMPH)
          {
            setpoint = setpoint * mphFactor;
          }
          requestedSpeed = setpoint;
          feedforwardDuty = speedToPwmDuty(setpoint); // raw 12-bit feed-forward base
          uint16_t trimmed = (uint16_t)applyFeedbackTrim(setpoint, feedforwardDuty);
          setMotorDutyRaw(trimmed);
          appliedDutyCycle = trimmed;
        }
      }
      else
      {
        testSpeed(); // open-loop speed test
      }
    }

    // Normal operation: convert incoming pulses to motor speed
    if (!testSpeedo && !testCal)
    {
      // Sample once per received hall pulse (tracked by the ISR tick stamp), not
      // only when the frequency value changes: a rock-steady input never changed
      // the reading, so the median never refilled and the incoming speed could sit
      // frozen/blank while the motor still ran.
      static TickType_t lastSampledPulse = 0;
      if (lastPulse != lastSampledPulse)
      {
        lastSampledPulse = lastPulse;
        DEBUG_CTRL("in freq=%lu Hz", (unsigned long)dutyCycleIncoming);

        // Clamp the incoming frequency to the configured hall range before mapping
        // (and guard maxFreqHall==0): an over-range signal or noise burst would
        // otherwise map above maxSpeed and peg the motor at full scale.
        uint16_t mappedSpeed = 0;
        if (maxFreqHall > 0)
        {
          unsigned long hallFreq = dutyCycleIncoming;
          if (hallFreq > maxFreqHall)
            hallFreq = maxFreqHall;
          mappedSpeed = (uint16_t)map(hallFreq, 0, maxFreqHall, 0, maxSpeed);
        }
        DEBUG_CTRL("mapped speed=%u kph", mappedSpeed);

        // Collect samples for median filtering
        if (rawCount < averageFilter)
        {
          samples.add(mappedSpeed);
          rawCount++;
        }

        // Once we have enough samples, calculate median and apply offset
        if (rawCount >= averageFilter)
        {
          dutyCycle = samples.getMedian();
          DEBUG_CTRL("median speed=%u kph", dutyCycle);

          // Speed display updated via API /api/status
          // Live speed value available in dutyCycle variable
          uint16_t finalSpeed = applyConfiguredSpeedOffset(dutyCycle);
          finalSpeed = finalSpeed * speedMultiplier;
          if (convertToMPH)
          {
            finalSpeed = finalSpeed * mphFactor;
          }
          requestedSpeed = finalSpeed;                      // offset-applied target speed (PID setpoint)
          feedforwardDuty = speedToPwmDuty(requestedSpeed); // raw 12-bit feed-forward base (custom-cal aware)
          if (!pidActive)
          {
            setMotorDutyRaw(feedforwardDuty);
            appliedDutyCycle = feedforwardDuty;
            DEBUG_CTRL("open-loop: %u kph -> pwm=%u/%u", requestedSpeed,
                       feedforwardDuty, (1u << PWM_RESOLUTION) - 1);
          }
          rawCount = 0;
          samples.clear();
        }
      }

      // Closed-loop trim: every 100 ms nudge the base duty so measured speed
      // (GPIO4 feedback) tracks the requested speed. Uses calibration as feed-forward.
      if (pidActive && (xTaskGetTickCount() - lastPidTick) >= pdMS_TO_TICKS(100))
      {
        lastPidTick = xTaskGetTickCount();
        uint16_t trimmed = (uint16_t)applyFeedbackTrim(requestedSpeed, feedforwardDuty);
        setMotorDutyRaw(trimmed);
        appliedDutyCycle = trimmed;
      }
    }

    // Keep the motor feedback readout live whenever the PID isn't running (feedback off,
    // or feedback not yet available) in any mode so the speed display and cal-capture
    // auto-calibration always have a fresh measurement — and so feedbackAvailable can
    // latch as soon as a real tacho signal appears. With the PID active,
    // applyFeedbackTrim already measures.
    if (!pidActive &&
        (xTaskGetTickCount() - lastMeasTick) >= pdMS_TO_TICKS(100))
    {
      lastMeasTick = xTaskGetTickCount();
      updateMeasuredFreq();
    }

    // Feedback-availability signalling for the UI: once the motor has been running
    // for a moment with no tacho signal seen, mark it missing so the dashboard can
    // show "N/A" instead of "--". Clears automatically once feedback appears.
    const TickType_t FB_DETECT_TICKS = pdMS_TO_TICKS(1500);
    if (appliedDutyCycle > 0)
    {
      if (motorRunSince == 0)
      {
        motorRunSince = xTaskGetTickCount();
      }
    }
    else
    {
      motorRunSince = 0;
    }
    feedbackMissing = (!feedbackAvailable && motorRunSince != 0 &&
                       (xTaskGetTickCount() - motorRunSince) > FB_DETECT_TICKS);

    // Delay until next cycle
    vTaskDelay(1);
  }
}
