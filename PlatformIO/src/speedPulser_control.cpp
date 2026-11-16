#include "speedPulser_control.h"
#include "speedPulser_defs.h"
#include "speedPulser_calBuilder.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"

// See speedPulser_control.h for the path from the speed sensor to the motor.

// ===== Live values =====
float inputFrequencyHz = 0.0f;
float inputSpeedKph = 0.0f;
uint16_t needleTargetSpeed = 0;
float speedOffsetApplied = 0.0f;
uint16_t motorDutyApplied = 0;
uint16_t calibrationProbeDuty = 0;

// ============================================================================
// Speed sensor input
// ============================================================================
// The interrupt keeps the time of the last INPUT_EDGE_HISTORY pulses; every
// 100 ms the speed is worked out from them: walk back from the newest pulse until
// the pulses cover inputSmoothingMs (or the history runs out), frequency =
// intervals / time. More pulses averaged = a steadier needle; a slow sensor still
// updates on every pulse. Between pulses the reading holds, but no longer than
// the car could still be moving that fast: with no pulse for `age`, the speed
// can't be more than 1.5 / age, so a stopping car reads down smoothly, and 0
// after the input timeout (V5.09: INPUT_TIMEOUT_MS, longer for a slow sensor so
// it doesn't flicker to 0 between pulses; see updateInputGlitchFilter()).
// Once stopped the history is forgotten: micros() wraps every 71.6 minutes, and
// a parked car's last pulses would otherwise look new again for a moment.
//
// Glitch filter: a pulse closer to the last one than half a period at Max Speed
// can't be real (ignition noise couples in as sub-millisecond bursts) and is
// dropped, keeping the last good pulse. The limit follows the sensor setting, so
// a fast sensor (e.g. 16000 pulses per mile, ~550 Hz at 200 km/h) isn't cut.
//
// IRAM_ATTR: the interrupt must run while the flash cache is off (settings save).
#define INPUT_EDGE_HISTORY 64 // power of 2
static volatile uint32_t inputEdgeUs[INPUT_EDGE_HISTORY]; // micros() of the newest pulses
static volatile uint32_t inputEdgeCount = 0;              // pulses accepted since boot
static volatile uint32_t inputGlitchUs = 2500;            // see updateInputGlitchFilter()
static uint32_t inputTimeoutUs = (uint32_t)INPUT_TIMEOUT_MS * 1000UL; // see updateInputGlitchFilter()
static volatile uint16_t inputPulsesForLed = 0;
static portMUX_TYPE inputMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR speedInputPulse()
{
  const uint32_t now = micros();
  const uint32_t n = inputEdgeCount;
  if (n && (now - inputEdgeUs[(n - 1) & (INPUT_EDGE_HISTORY - 1)]) < inputGlitchUs)
    return; // noise: keep the last good pulse
  inputEdgeUs[n & (INPUT_EDGE_HISTORY - 1)] = now;
  inputEdgeCount = n + 1;
  inputPulsesForLed = inputPulsesForLed + 1;
}

void updateInputGlitchFilter()
{
  const float fullScaleHz = (float)maxSpeed * sensorPulsesPerKm / 3600.0f;
  uint32_t us = fullScaleHz > 1.0f ? (uint32_t)(500000.0f / fullScaleHz) : 3000;
  us = constrain(us, (uint32_t)INPUT_GLITCH_MIN_US, (uint32_t)INPUT_GLITCH_MAX_US);
  inputGlitchUs = us;

  // Stopped after INPUT_TIMEOUT_SLOW_PERIODS pulse periods at INPUT_TIMEOUT_SLOW_KPH,
  // within INPUT_TIMEOUT_MS..INPUT_TIMEOUT_MAX_MS: a 100 pulses/km sensor gives one
  // pulse every 1.8 s at 20 km/h, which the fixed 1.5 s read as stopped.
  const float slowPeriodMs = 3600000.0f / (INPUT_TIMEOUT_SLOW_KPH * sensorPulsesPerKm);
  const float timeoutMs = constrain(INPUT_TIMEOUT_SLOW_PERIODS * slowPeriodMs, (float)INPUT_TIMEOUT_MS,
                                    (float)INPUT_TIMEOUT_MAX_MS);
  inputTimeoutUs = (uint32_t)(timeoutMs * 1000.0f);
  DEBUG_CTRL("input: %.0f pulses/km, full scale %.0f Hz -> glitch filter %lu us, stopped after %.0f ms",
             sensorPulsesPerKm, fullScaleHz, (unsigned long)us, timeoutMs);
}

static float measureInputFrequency()
{
  uint32_t edges[INPUT_EDGE_HISTORY];
  portENTER_CRITICAL(&inputMux);
  const uint32_t n = inputEdgeCount;
  for (uint8_t i = 0; i < INPUT_EDGE_HISTORY; i++)
    edges[i] = inputEdgeUs[i];
  portEXIT_CRITICAL(&inputMux);
  if (n < 2)
    return 0.0f;

  const uint32_t timeoutUs = inputTimeoutUs;
  const uint32_t newest = edges[(n - 1) & (INPUT_EDGE_HISTORY - 1)];
  const uint32_t age = micros() - newest;
  if (age >= timeoutUs)
  {
    portENTER_CRITICAL(&inputMux);
    if (inputEdgeCount == n)
      inputEdgeCount = 0; // stopped: forget the old pulses (see the micros() note above)
    portEXIT_CRITICAL(&inputMux);
    return 0.0f;
  }

  const uint32_t wantedSpanUs = (uint32_t)inputSmoothingMs * 1000UL;
  const uint32_t maxIntervals = (n - 1 < INPUT_EDGE_HISTORY - 1) ? n - 1 : INPUT_EDGE_HISTORY - 1;
  uint32_t intervals = 0, spanUs = 0;
  for (uint32_t i = 1; i <= maxIntervals; i++)
  {
    const uint32_t span = newest - edges[(n - 1 - i) & (INPUT_EDGE_HISTORY - 1)];
    if (span - spanUs > timeoutUs)
      break; // the car was stopped before this pulse
    intervals = i;
    spanUs = span;
    if (spanUs >= wantedSpanUs)
      break;
  }
  if (intervals == 0 || spanUs == 0)
    return 0.0f;
  float hz = (float)intervals * 1000000.0f / (float)spanUs;
  if (hz * (float)age > 1500000.0f)
    hz = 1500000.0f / (float)age; // next pulse overdue: slowing down
  return hz;
}

// Flash the onboard LED with the input: one toggle per 6 pulses, read on the tick.
static void flashInputLed()
{
  static bool ledOn = false;
  if (inputPulsesForLed >= 6)
  {
    inputPulsesForLed = 0;
    ledOn = !ledOn;
    digitalWrite(pinOnboardLED, ledOn);
  }
}

// ===== Speed offset + units =====
// Global: one fixed km/h added to every speed. Curve: an offset at 25 / 75 / 125 /
// 175 / 225 km/h, straight lines between them, flat beyond the ends (so the
// needle never jumps as the speed crosses from one band into the next).
static const float CURVE_POINT_KPH[SPEED_OFFSET_CURVE_POINTS] = {25.0f, 75.0f, 125.0f, 175.0f, 225.0f};

static float curveOffsetAt(float kph)
{
  const int16_t *off = speedOffsetCurveOffsets;
  if (kph <= CURVE_POINT_KPH[0])
    return off[0];
  for (uint8_t i = 1; i < SPEED_OFFSET_CURVE_POINTS; i++)
  {
    if (kph <= CURVE_POINT_KPH[i])
    {
      const float t = (kph - CURVE_POINT_KPH[i - 1]) / (CURVE_POINT_KPH[i] - CURVE_POINT_KPH[i - 1]);
      return off[i - 1] + t * (float)(off[i] - off[i - 1]);
    }
  }
  return off[SPEED_OFFSET_CURVE_POINTS - 1];
}

uint16_t toNeedleTarget(float speedKph)
{
  if (speedKph <= 0.0f)
  {
    speedOffsetApplied = 0.0f;
    return 0; // standing still: no offset, needle at rest
  }
  const float offset = useSpeedOffsetCurve ? curveOffsetAt(speedKph) : (float)globalSpeedOffsetKph;
  speedOffsetApplied = offset;
  float target = speedKph + offset;
  if (target < 0.0f)
    target = 0.0f;
  if (clusterInMph)
    target *= KPH_TO_MPH;
  long rounded = lroundf(target); // round once, at the end
  if (rounded > 400)
    rounded = 400;
  return (uint16_t)rounded;
}

// ============================================================================
// Legacy board: speed -> PWM duty
// ============================================================================
// presetSpeedTable[] maps each 10-bit duty (0..384) to the speed it shows. Leading
// entries are 0 (the motor won't turn) and the rest rise. Return the duty whose
// speed is nearest to `speed`; on a tie the lower duty. 0 = at or below the
// lowest speed the motor can hold (motor off).
static uint16_t findClosestMatch(uint16_t speed)
{
  const uint16_t n = sizeof presetSpeedTable / sizeof presetSpeedTable[0];
  uint16_t bestIdx = 0, minSpeed = 0;
  uint32_t bestErr = UINT32_MAX;
  bool haveCal = false;
  for (uint16_t i = 0; i < n; i++)
  {
    const uint16_t spd = presetSpeedTable[i];
    if (spd == 0)
      continue; // dead zone
    if (!haveCal)
    {
      minSpeed = spd;
      haveCal = true;
    }
    const uint32_t err = (speed > spd) ? (uint32_t)(speed - spd) : (uint32_t)(spd - speed);
    if (err < bestErr)
    {
      bestErr = err;
      bestIdx = i;
    }
  }
  if (!haveCal || speed <= minSpeed)
    return 0;
  return bestIdx;
}

// Snap to the nearest table duty, then interpolate towards the neighbour that
// brackets the requested speed, so the 12-bit hardware gets sub-step duty.
// The custom (Calibration Builder) table interpolates straight from its points.
uint32_t speedToPwmDuty(uint16_t speed)
{
  if (presetId == CUSTOM_CAL_ID && customCalValid)
    return customSpeedToDuty12(speed);

  const int32_t maxIdx = (int32_t)(sizeof presetSpeedTable / sizeof presetSpeedTable[0]) - 1; // 384
  const uint16_t nearIdx = findClosestMatch(speed);
  if (nearIdx == 0)
    return 0;
  const int32_t nearSpeed = (int32_t)presetSpeedTable[nearIdx];
  const uint32_t base = (uint32_t)nearIdx << DUTY_SCALE_SHIFT;
  if (nearSpeed == (int32_t)speed)
    return base;

  // Step to the neighbour on the side of the requested speed, skipping flat entries.
  const int32_t dir = ((int32_t)speed > nearSpeed) ? +1 : -1;
  int32_t otherIdx = (int32_t)nearIdx + dir;
  while (otherIdx >= 0 && otherIdx <= maxIdx && presetSpeedTable[otherIdx] == (uint16_t)nearSpeed)
    otherIdx += dir;
  if (otherIdx < 0 || otherIdx > maxIdx || presetSpeedTable[otherIdx] == 0)
    return base;
  const int32_t otherSpeed = (int32_t)presetSpeedTable[otherIdx];
  const int32_t speedSpan = otherSpeed - nearSpeed;
  if (speedSpan == 0)
    return base;

  const int32_t num = ((int32_t)speed - nearSpeed) * (otherIdx - (int32_t)nearIdx);
  int32_t pwm = (int32_t)base + ((num << DUTY_SCALE_SHIFT) / speedSpan);
  const int32_t maxPwm = maxIdx << DUTY_SCALE_SHIFT;
  if (pwm < 0)
    pwm = 0;
  if (pwm > maxPwm)
    pwm = maxPwm;
  return (uint32_t)pwm;
}

// ============================================================================
// Motor tacho (voltage board)
// ============================================================================
// The C3 has no pulse counter, so the tacho on GPIO4 is timed in software: this
// interrupt keeps the micros() of the last FB_TS_N falling edges, and
// updateMeasuredFreq() works the frequency out from them.
//
// V5.03 counted edges per fixed 100 ms window and needed two in a window, so
// below ~20 Hz windows read 0 Hz while the motor was still turning (bench,
// 2026-11-15: 14 of 29 windows read 0 at 15 Hz, all of them at 8.5 Hz). Timing
// the edges themselves has no such floor.
//
// Edges closer than FB_GLITCH_US are line noise (the top speed is ~254 Hz =
// 3.9 ms; the bench saw 0.02-0.6 ms pairs on the tacho line) and are dropped.
#define FB_TS_N 32                // edge history (power of 2): 6 revolutions + 2
static volatile uint32_t fbTs[FB_TS_N]; // micros() of the newest edges
static volatile uint32_t fbSeq = 0;     // edges accepted since boot
#ifdef BENCH_SERIAL
void benchRecordFalling(uint32_t now); // speedPulser_bench.cpp (bench build only)
#endif
void IRAM_ATTR feedbackPulse()
{
  const uint32_t now = micros();
#ifdef BENCH_SERIAL
  benchRecordFalling(now);
#endif
  const uint32_t s = fbSeq;
  if (s && (now - fbTs[(s - 1) & (FB_TS_N - 1)]) < FB_GLITCH_US)
    return; // noise: keep the earlier edge
  fbTs[s & (FB_TS_N - 1)] = now;
  fbSeq = s + 1;
}

float measuredFreqHz = 0.0f;    // motor speed (Hz), smoothed
float measuredFreqRawHz = 0.0f; // motor speed (Hz), unsmoothed
static float measFreqFilt = 0.0f;
static portMUX_TYPE feedbackMux = portMUX_INITIALIZER_UNLOCKED;

// Dial speed <-> tacho Hz, through the drive calibration marks (piecewise, so a
// dial that isn't linear near zero still reads right). No table = 0.
static bool useDriveTable()
{
  return boardHasVoltageControl && driveCalHzValid();
}

static float tachoHzToSpeed(float hz)
{
  return useDriveTable() ? speedForHz(hz) : 0.0f;
}

// ===== Capture window =====
// The last ~1 s of RAW tacho readings (one per 100 ms measurement). The capture
// averages over it and refuses a reading that is still moving, so a mark isn't
// taken mid-transition just after the drive was nudged.
#define FB_RING 10
static float fbRing[FB_RING] = {0};
static uint8_t fbRingIdx = 0;
static uint8_t fbRingCount = 0;

static void pushFeedbackSample(float hz)
{
  fbRing[fbRingIdx] = hz;
  fbRingIdx = (fbRingIdx + 1) % FB_RING;
  if (fbRingCount < FB_RING)
    fbRingCount++;
}

bool feedbackHzWindow(float &meanHz, float &spreadPct, float &driftHz)
{
  meanHz = 0.0f;
  spreadPct = 0.0f;
  driftHz = 0.0f;
  if (fbRingCount < FB_RING)
    return false;
  float sum = 0.0f, older = 0.0f, lo = fbRing[0], hi = fbRing[0];
  for (uint8_t k = 0; k < FB_RING; k++)
  {
    const float v = fbRing[(fbRingIdx + k) % FB_RING]; // k = 0 is the oldest
    sum += v;
    if (k < FB_RING / 2) older += v;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  meanHz = sum / (float)FB_RING;
  if (meanHz < 1.0f)
    return false;
  spreadPct = (hi - lo) * 100.0f / meanHz;
  const float half = (float)(FB_RING / 2);
  driftHz = (sum - older) / half - older / half;
  return true;
}

// Motor frequency (Hz) from the edge timestamps, smoothed, published to
// measuredFreqHz/measuredSpeed. Called once per 100 ms by the control task in
// EVERY mode, so the read-outs, the capture window and the drive trim share one
// fresh measurement.
//
// From the newest edge, walk back over whole revolutions (multiples of
// FB_PULSES_PER_REV intervals: the poles aren't evenly spaced, and a whole turn
// cancels that) until the span covers FB_SPAN_US, or the history runs out.
// freq = intervals / span. At 254 Hz that is 6 turns in ~120 ms; at 8 Hz one turn
// in ~0.6 s. Never averages across a gap longer than FB_STALL_US (the motor was
// stopped there). Between edges the reading holds, but no longer than the motor
// could plausibly be turning: with no edge for `age`, it can't be faster than
// 1.5 / age, so a stopping motor reads down smoothly, and 0 after FB_STALL_US.
float updateMeasuredFreq()
{

  uint32_t ts[FB_TS_N];
  portENTER_CRITICAL(&feedbackMux);
  const uint32_t seq = fbSeq;
  for (uint8_t i = 0; i < FB_TS_N; i++)
    ts[i] = fbTs[i];
  portEXIT_CRITICAL(&feedbackMux);
  const uint32_t nowUs = micros();

  float measuredFreq = 0.0f;
  uint32_t kUsed = 0;
  if (seq >= 2)
  {
    const uint32_t newest = ts[(seq - 1) & (FB_TS_N - 1)];
    const uint32_t age = nowUs - newest;
    if (age >= FB_STALL_US)
    {
      // Stopped: forget the old edges, or once micros() wraps (71.6 min) they look new.
      portENTER_CRITICAL(&feedbackMux);
      if (fbSeq == seq)
        fbSeq = 0;
      portEXIT_CRITICAL(&feedbackMux);
    }
    else
    {
      const uint32_t maxK = (seq - 1 < FB_TS_N - 1) ? seq - 1 : FB_TS_N - 1;
      uint32_t k = 0, span = 0, kRev = 0, spanRev = 0;
      for (uint32_t i = 1; i <= maxK; i++)
      {
        const uint32_t s = newest - ts[(seq - 1 - i) & (FB_TS_N - 1)];
        if (s - span > FB_STALL_US)
          break; // the motor was stopped before this edge
        k = i;
        span = s;
        if (k % FB_PULSES_PER_REV == 0)
        {
          kRev = k;
          spanRev = span;
          if (span >= FB_SPAN_US)
            break;
        }
      }
      if (kRev) // whole turns available: use them
      {
        k = kRev;
        span = spanRev;
      }
      if (k && span)
      {
        measuredFreq = (float)k * 1000000.0f / (float)span;
        if (measuredFreq * (float)age > 1500000.0f)
          measuredFreq = 1500000.0f / (float)age; // overdue edge: slowing down
        kUsed = k;
      }
    }
  }
  measuredFreqRawHz = measuredFreq;
  pushFeedbackSample(measuredFreq); // capture window

  if (measuredFreq > 0.0f && kUsed >= 2)
    feedbackAvailable = true; // a genuine tacho edge stream proves the feedback pin is wired

  if (measuredFreq <= 0.0f)
    measFreqFilt = 0.0f;
  else
    measFreqFilt += FB_MEAS_ALPHA * (measuredFreq - measFreqFilt);
  measuredFreqHz = measFreqFilt;
  measuredSpeed = (uint16_t)lroundf(tachoHzToSpeed(measFreqFilt)); // cluster units, for the UI
  return measFreqFilt;
}

// ============================================================================
// Voltage board: drive control
// ============================================================================
// Open loop first: the drive for a target speed comes straight from the
// calibration table (speedPulser_driveCal), so with the marks captured the needle
// lands where it should without any feedback at all. No marks = no drive.
//
// With Feedback Enable on (and a tacho, and a table with Hz), ONE slow integrator
// nudges that drive to hold the calibrated tacho Hz against load and temperature
// drift. It does nothing inside the deadband, nothing below the minimum feedback
// speed, and is capped at ±trimLimit.
float driveTrim = 0.0f;

// Reference model (V5.04): the tacho Hz the motor should be showing NOW, following
// the target the way the motor + measurement do (bench, 2026-11-15: 63 % of a step
// in ~0.35 s, 90 % in ~0.55 s, up or down, 10-160 km/h). The trim works on the
// error against this, and only once it has (nearly) reached the target: getting
// to a new speed is the calibration's job. Integrating the raw step error instead
// wound the trim up while the motor was still accelerating: 5-31 Hz overshoot on a
// speed-up, and on a big slow-down the needle dipped to ~0 and took ~7 s to come back.
static float refHz = 0.0f;

// Tacho lost (V5.08): feedbackAvailable latches once a tacho is seen, so a wire that
// breaks later read as "the motor is stopped" and the trim wound up to +trimLimit
// in a fraction of a second (needle ~40 % high). If the tacho reads nothing for
// FB_LOST_TICKS while the target is above the motor's floor, the trim is dropped
// (open loop, straight from the calibration) and the page says the feedback is
// missing. A reading far from the model (noise, a bad edge) is never integrated.
static uint8_t tachoSilentTicks = 0;
static bool tachoLost = false;

void resetDriveControl()
{
  driveTrim = 0.0f;
  refHz = 0.0f;
  tachoSilentTicks = 0;
}

uint16_t applyDriveControl(uint16_t targetSpeed)
{
  const float PERIOD_S = CONTROL_PERIOD_MS / 1000.0f;

  if (targetSpeed == 0)
  {
    resetDriveControl();
    return applyMotorDrive(0.0f);
  }

  // Cap at the top calibration mark: past it both the drive and the trim's target
  // Hz would be extrapolated, and the trim would happily chase an overspin.
  const uint16_t topSpeed = driveCalTopSpeed();
  if (topSpeed > 0 && targetSpeed > topSpeed)
    targetSpeed = topSpeed;

  const float ffDrive = driveForSpeed(targetSpeed);
  if (feedbackAvailable && measuredFreqRawHz <= 0.0f && hzForSpeed((float)targetSpeed) > FB_LOST_MIN_HZ)
  {
    if (tachoSilentTicks < 255)
      tachoSilentTicks++;
  }
  else
  {
    tachoSilentTicks = 0;
  }
  tachoLost = tachoSilentTicks >= FB_LOST_TICKS;
  const bool closedLoop = feedbackEnable && feedbackAvailable && !tachoLost && driveCalHzValid() &&
                          !(feedbackMinSpeed > 0 && targetSpeed < feedbackMinSpeed);
  float tgtHz = 0.0f;
  if (closedLoop)
  {
    tgtHz = hzForSpeed((float)targetSpeed);
    if (refHz <= 0.0f)
      refHz = measuredFreqHz; // loop just engaged: start the model where the motor is
    refHz += (tgtHz - refHz) * (PERIOD_S / (FB_REF_TAU_S + PERIOD_S));
    const float errHz = refHz - measuredFreqHz;
    const float deadbandHz = feedbackDeadband > 0.0f ? feedbackDeadband : 0.0f;
    const bool settled = fabsf(tgtHz - refHz) <= FB_REF_SETTLED * tgtHz;
    const bool plausible = fabsf(errHz) <= FB_PLAUSIBLE * refHz;
    if (tgtHz > 0.5f && settled && plausible && fabsf(errHz) > deadbandHz)
    {
      driveTrim += trimGain * (errHz / tgtHz) * PERIOD_S;
      driveTrim = constrain(driveTrim, -trimLimit, trimLimit);
    }
  }
  else
  {
    driveTrim = 0.0f;
    refHz = 0.0f;
  }

  // Never let the trim push the drive more than 5 % past the top proven mark:
  // past it nobody has seen where the needle goes (a pegged speedo is damaged), nor
  // past the most the rail can give.
  float drive = ffDrive * (1.0f + driveTrim);
  float driveCap = driveMaxV();
  if (topSpeed > 0 && driveForSpeed(topSpeed) * 1.05f < driveCap)
    driveCap = driveForSpeed(topSpeed) * 1.05f;
  if (drive > driveCap)
  {
    drive = driveCap;
    // Anti-windup (V5.09): the trim stops where the drive stops. It used to keep
    // integrating against the cap up to +trimLimit, then held the needle high for
    // seconds after the speed dropped.
    if (closedLoop && ffDrive > 0.0f && driveTrim > drive / ffDrive - 1.0f)
      driveTrim = drive / ffDrive - 1.0f;
  }
  const uint16_t duty = applyMotorDrive(drive);

  static uint8_t logDiv = 0;
  if (++logDiv >= 10)
  {
    logDiv = 0;
    DEBUG_FB("drive: tgt=%u ff=%.2f V trim=%+.1f%% -> %.2f V (rail %.2f V x %.0f%%) | tgtF=%.1f measF=%.1f%s",
             targetSpeed, ffDrive, driveTrim * 100.0f, lastDriveV, lastRailCmdV,
             lastPwmFrac * 100.0f, tgtHz, measuredFreqHz, closedLoop ? "" : " (open loop)");
  }
  return duty;
}

bool motorIsStopped()
{
  return motorDutyApplied == 0 && lastDriveV <= 0.0f && inputSpeedKph <= 0.0f && !speedTestOn;
}

// ============================================================================
// The control task: one 100 ms tick for everything
// ============================================================================
void speedControlTask(void *parameter)
{
  TickType_t lastWake = xTaskGetTickCount();
  TickType_t motorRunSince = 0; // when the motor started running with no tacho seen yet
  uint8_t logDiv = 0;
  for (;;)
  {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

    // Test Board / the bench own the motor and the rail while they run.
    if (boardTestActive || benchActive)
      continue;

    // Measure everything first, in every mode, so the read-outs never freeze.
    if (boardHasVoltageControl)
      updateMotorRail();
    updateMeasuredFreq();
    autoFillTick(); // Auto Fill sets the calibration drive (no-op unless running)
    inputFrequencyHz = measureInputFrequency();
    float kph = inputFrequencyHz * 3600.0f / sensorPulsesPerKm;
    if (kph > (float)maxSpeed)
      kph = (float)maxSpeed; // faster than the dial: hold at Max Speed
    inputSpeedKph = kph;
    flashInputLed();

    if (needleSweepRequested)
    {
      needleSweep(); // blocks for a few seconds; only allowed with the car stopped
      needleSweepRequested = false;
      lastWake = xTaskGetTickCount();
      continue;
    }

    if (calibrationModeOn)
    {
      // The user (or Auto Fill) sets the motor directly: the drive in volts on the
      // voltage board, the raw PWM duty on a legacy board.
      needleTargetSpeed = 0;
      if (boardHasVoltageControl)
      {
        motorDutyApplied = applyMotorDrive(calibrationDriveV);
      }
      else
      {
        setMotorDutyRaw(calibrationProbeDuty);
        motorDutyApplied = calibrationProbeDuty;
      }
    }
    else
    {
      needleTargetSpeed = toNeedleTarget(speedTestOn ? (float)speedTestKph : inputSpeedKph);
      if (boardHasVoltageControl)
      {
        motorDutyApplied = applyDriveControl(needleTargetSpeed);
      }
      else
      {
        const uint32_t duty = speedToPwmDuty(needleTargetSpeed);
        setMotorDutyRaw(duty);
        motorDutyApplied = (uint16_t)duty;
      }
    }

    // Voltage board: once the motor has run for a moment with no tacho seen, tell
    // the UI the feedback signal is missing. Legacy boards have no tacho.
    if (motorDutyApplied > 0)
    {
      if (motorRunSince == 0)
        motorRunSince = xTaskGetTickCount();
    }
    else
    {
      motorRunSince = 0;
    }
    feedbackMissing = boardHasVoltageControl && motorRunSince != 0 &&
                      ((!feedbackAvailable && (xTaskGetTickCount() - motorRunSince) > pdMS_TO_TICKS(1500)) ||
                       (tachoLost && !calibrationModeOn));

    if (++logDiv >= 10)
    {
      logDiv = 0;
      DEBUG_CTRL("input %.1f Hz = %.1f km/h -> needle %u%s (offset %+.1f) duty %u%s",
                 inputFrequencyHz, inputSpeedKph, needleTargetSpeed, clusterInMph ? " mph" : "",
                 speedOffsetApplied, motorDutyApplied,
                 calibrationModeOn ? " [CAL]" : (speedTestOn ? " [TEST]" : ""));
    }
  }
}
