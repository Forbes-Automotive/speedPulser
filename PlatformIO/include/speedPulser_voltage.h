#ifndef SPEED_PULSER_VOLTAGE_H
#define SPEED_PULSER_VOLTAGE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SpeedPulser — buck voltage control (voltage-injection PCB, "SpeedPulserV5" rev6)
//
// The voltage board adds an adjustable motor supply: an ESP PWM "DAC" (GPIO6,
// V_ADJ) is injected into the TPS62933 feedback node so firmware sets the motor
// rail, and GPIO7 (buck EN) gates the supply on/off. A 1k-to-GND strap on GPIO3
// marks the board so the SAME firmware runs on legacy (PWM-only) boards untouched.
//
// V5 adds the motor-rail read-back: the rail through R24 39k / R25 10k (+C33
// 100 nF) onto GPIO1 (ADC1_CH1, V_MOT_SENSE). The "Test Board" routine steps the
// rail and reads it back to prove the V_ADJ injection path AND the sense divider
// work, and stores a measured duty->volts fit for this board.
//
// The injection is INVERSE: a higher V_ADJ pulls FB up and LOWERS the motor
// rail. Rail commands are in volts and mapped to the inverse V_ADJ duty
// internally, so the rest of the firmware never has to think about the inversion.
//
// V5.01 — DRIVE: the motor is commanded with one number, the drive in volts
// (rail volts x PWM fraction, 0..Maximum Rail). Down to Minimum Rail x Nominal PWM
// the PWM is held at Nominal and only the rail moves (smooth low-voltage running);
// below that the rail sits at Minimum Rail and the PWM falls to zero, so the drive
// (Minimum Rail is fixed at RAIL_V_FLOOR, the most rail-only range there is)
// reaches all the way to a stopped needle. Calibration captures needle marks
// against drive (see speedPulser_driveCal.h).
// ---------------------------------------------------------------------------

// ===== Rail model (rev6 component values) =====
// Vrail = Vref*(1 + R6/R11) + (R6/Rinj)*(Vref - D*3V3)
//       = 0.8*(1 + 39k/3k) + (39k/12k)*(0.8 - D*3.3)    (Rinj = R8+R9+R23 = 12k)
//       = 13.8 - 10.725*D                                (D = V_ADJ duty 0..1)
// Used until the Test Board fit replaces it with this board's measured line.
#define RAIL_MODEL_V0      13.8f    // rail (V) at 0 % V_ADJ duty
#define RAIL_MODEL_SLOPE  -10.725f  // rail volts per unit V_ADJ duty
#define RAIL_V_FLOOR       3.0f     // ~100 % V_ADJ duty: lowest rail the injection can reach
#define RAIL_V_CEIL       12.0f     // firmware ceiling: motor rating, C21 16 V cap, and the
                                    // ADC read-back saturates at ~12.2 V (2.5 V at the pin)
#define RAIL_V_LEGACY      9.0f     // rail held when voltage control is OFF: the legacy duty
                                    // presets were captured on the old fixed ~9 V supply

// ===== Rail read-back divider (V_MOT_SENSE) =====
#define RAIL_SENSE_R_TOP_K 39.0f    // R24 (kΩ) rail -> GPIO1
#define RAIL_SENSE_R_BOT_K 10.0f    // R25 (kΩ) GPIO1 -> GND

// Board detection + live state
extern bool  boardHasVoltageControl; // true = voltage board (GPIO3 low); false = legacy PWM-only board
extern bool  buckEnabled;            // true once the buck supply has been enabled
extern float lastVoltageCmd;         // last commanded rail as 0..1 of RAIL_V_FLOOR..RAIL_V_CEIL (charts)
extern float lastRailCmdV;           // last commanded motor rail (V)
extern float lastPwmFrac;            // last motor PWM fraction applied by the drive (0..1)
extern float lastDriveV;             // last drive applied (V = rail x PWM)
extern float motorRailV;             // measured motor rail (V), smoothed (V_MOT_SENSE)

// User configuration (persisted in EEP)
extern bool  voltageControlEnable;   // master toggle; when off the board holds RAIL_V_LEGACY and runs legacy PWM
extern float vcPwmNominal;           // PWM fraction held while the rail does the work
extern float vcRailMaxV;             // highest rail used (V); the drive tops out at this x 100 % PWM
extern float vcTrimGain;             // feedback trim: relative drive change per second per unit relative Hz error
extern float vcTrimLimit;            // feedback trim authority (fraction of the calibrated drive, e.g. 0.2 = ±20 %)

// Calibration: drive applied in calibration mode (V)
extern float tempDriveV;

// ===== Board test (Test Board button) =====
enum BoardTestCode : uint8_t {
    BT_NOT_RUN      = 0,  // never run on this unit
    BT_PASS         = 1,  // V_ADJ injection + rail read-back both verified
    BT_LEGACY       = 2,  // no GPIO3 strap: legacy PWM-only board, nothing to test
    BT_NO_SENSE     = 3,  // rail reads ~0 V at every step: no V_MOT_SENSE divider, or buck not running
    BT_NO_RESPONSE  = 4,  // rail is there but does not follow V_ADJ
    BT_OUT_OF_RANGE = 5,  // follows V_ADJ but far from the expected voltages (check R6/R11/R8/R9/R23)
};
#define BOARD_TEST_POINTS 3
extern volatile bool boardTestActive;   // true while the test owns the motor/rail (control task yields)
extern uint8_t boardTestCode;           // BoardTestCode of the last run (persisted)
extern float   boardTestTargetV[BOARD_TEST_POINTS]; // model target for each step (V)
extern float   boardTestMeasV[BOARD_TEST_POINTS];   // measured rail at each step (V, persisted)
extern bool    railFitValid;            // true when railFitV0/railFitSlope came from a passed test
extern float   railFitV0;               // measured rail at 0 % V_ADJ duty (extrapolated)
extern float   railFitSlope;            // measured rail volts per unit V_ADJ duty (negative)

void detectBoardVersion();            // read GPIO3 once at boot -> boardHasVoltageControl
void initVoltageControl();            // configure V_ADJ PWM + buck-enable pin + rail ADC (voltage board only)
void setMotorRailVolts(float volts);  // command the rail in volts (clamped to RAIL_V_FLOOR..RAIL_V_CEIL)
float driveMaxV();                    // top of the drive range (V): Maximum Rail x 100 % PWM
uint16_t applyMotorDrive(float driveV); // split drive into rail + PWM, apply both; returns the raw PWM duty
void enableBuck();                    // set a safe (low) rail, then drive EN high
void disableBuck();                   // drive EN low (supply off)

float readMotorRailVolts();           // one averaged ADC read of the rail (V); 0 on a legacy board
void  updateMotorRail();              // smoothed read into motorRailV — call on a ~100 ms cadence
bool  railLimited();                  // rail sitting well below its command (supply too low / overload)

bool  startBoardTest();               // kick off the Test Board task; false if already running
const char *boardTestText(uint8_t code);

#endif // SPEED_PULSER_VOLTAGE_H
