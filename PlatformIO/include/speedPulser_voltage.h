#ifndef SPEED_PULSER_VOLTAGE_H
#define SPEED_PULSER_VOLTAGE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SpeedPulser V4 — buck voltage control
//
// The V4 PCB adds an adjustable motor supply: an ESP PWM "DAC" (GPIO6, V_ADJ)
// is injected into the TPS62933 feedback node so firmware sets the motor rail,
// and GPIO7 (buck EN) gates the supply on/off. A 1k-to-GND strap on GPIO3 marks
// the new board so the SAME firmware runs on legacy (PWM-only) boards untouched.
//
// The injection is INVERSE: a higher V_ADJ pulls FB up and LOWERS the motor
// rail. Motor-voltage commands here are normalised 0..1 (1 = maximum volts) and
// mapped to the inverse V_ADJ duty internally, so the rest of the firmware never
// has to think about the inversion.
// ---------------------------------------------------------------------------

// Board detection + live state
extern bool  boardHasVoltageControl; // true = V4 board (GPIO3 low); false = legacy PWM-only board
extern bool  buckEnabled;            // true once the buck supply has been enabled
extern float lastVoltageCmd;         // last commanded motor-voltage level (0..1, 1 = max volts)
extern float lastPwmFrac;            // last throttle-PWM fraction the control loop applied (0..1)

// User configuration (persisted in EEP)
extern bool  voltageControlEnable;   // master toggle; when off the V4 board runs legacy PWM at full volts
extern float vcPwmNominal;           // throttle-PWM fraction the slow voltage loop keeps the fast loop centred on
extern float vcPwmMin;               // minimum throttle-PWM fraction (inner-loop clamp)
extern float vcVoltMin;              // minimum motor-voltage command (0..1)
extern float vcVoltMax;              // maximum motor-voltage command (0..1)
extern float vcVoltGain;             // slow voltage-trim integrator gain (Kv)
extern float vcKp;                   // inner RPM PID proportional gain (normalised freq error)
extern float vcKi;                   // inner RPM PID integral gain
extern float vcKd;                   // inner RPM PID derivative gain

// Manual calibration: motor-voltage command applied in calibration/test mode (0..1)
extern float tempVoltageCmd;

void detectBoardVersion();            // read GPIO3 once at boot -> boardHasVoltageControl
void initVoltageControl();            // configure V_ADJ PWM + buck-enable pin (V4 board only)
void setMotorVoltageCmd(float cmd01); // 0..1 (1 = max motor volts); inverse-maps to the V_ADJ duty
void enableBuck();                    // set a safe (low) rail, then drive EN high
void disableBuck();                   // drive EN low (supply off)

#endif // SPEED_PULSER_VOLTAGE_H
