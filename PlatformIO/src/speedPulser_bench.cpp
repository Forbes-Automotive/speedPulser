#include "speedPulser_defs.h"
#include "speedPulser_control.h"
#include "speedPulser_voltage.h"
#include "speedPulser_driveCal.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

// ---------------------------------------------------------------------------
// Serial bench interface — BENCH BUILD ONLY (-D BENCH_SERIAL, env:bench).
//
// Lets a PC script (tools/bench_tacho.py) own the motor directly: set the rail
// and the raw PWM duty, and receive every raw tacho edge (both edges, with the
// pin level) so the low-speed signal can be analysed off-board.
//
// Safety (on top of the RAIL_HARD_MAX_V cap in writeVadjDuty and the
// RAIL_TRIP_V read-back trip in updateMotorRail):
//  - rail requests are clamped to RAIL_V_FLOOR..RAIL_HARD_MAX_V here as well;
//  - no command for BENCH_WATCHDOG_MS -> PWM 0 (script crashed / unplugged);
//  - the rail is read every 20 ms while bench mode is on (trip check).
//
// Protocol (one command per line, replies start with OK / ERR):
//   bench 1 | bench 0     enter / leave bench mode (leave = PWM 0, rail back to normal)
//   rail <volts>          set the motor rail (clamped)
//   duty <0..4095>        raw 12-bit motor PWM duty
//   ping                  keep-alive
// While in bench mode the board sends:
//   S,<ms>,<railCmd>,<railMeas>,<duty>,<edges>,<lost>,<trip>   every 100 ms
//   E,<t>,<t>,...         tacho edges: micros() with bit0 = pin level after the edge
// ---------------------------------------------------------------------------

volatile bool benchActive = false;

#ifdef BENCH_SERIAL

#ifndef RAIL_HARD_MAX_V
#error "bench build needs RAIL_HARD_MAX_V"
#endif

#define BENCH_RING 1024 // power of 2
static volatile uint32_t benchTs[BENCH_RING];
static volatile uint32_t benchHead = 0; // edges written (ISR)
static uint32_t benchTail = 0;          // edges sent (task)
static uint32_t benchLost = 0;
static uint32_t benchDuty = 0;
static const uint32_t BENCH_WATCHDOG_MS = 1500;
static float benchMaxHz = 120.0f;   // overspeed trip (tacho Hz), set with "maxhz"
static volatile bool benchOverspeed = false;

static bool benchTele = false;       // stream S/E/T lines outside bench mode too
static DriveCalPoint benchCal[DRIVE_CAL_MAX]; // staged marks (calpt)
static uint8_t benchCalN = 0;
static bool benchSpeedOwned = false; // Speed Test was started by the bench ("speed") - watchdog stops it
static bool benchAfOwned = false;    // Auto Fill was started by the bench ("af 1") - watchdog stops it

// Tacho Hz from the newest falling edges in the ring (glitches < 1 ms skipped);
// 0 when the newest edge is older than 500 ms. Independent of the firmware's own
// measurement, so it guards the control loop too.
static float benchTachoHz()
{
    const uint32_t head = benchHead;
    const uint32_t nowUs = micros();
    uint32_t t[6];
    uint8_t k = 0;
    for (uint32_t i = 1; i <= 64 && i <= head && k < 6; i++)
    {
        const uint32_t w = benchTs[(head - i) & (BENCH_RING - 1)];
        if (w & 1u) continue;               // rising edge (bench CHANGE mode)
        const uint32_t tw = w & ~1u;
        if (k && (t[k - 1] - tw) < 1000u) continue; // glitch
        t[k++] = tw;
    }
    if (k < 3 || (nowUs - t[0]) > 500000u)
        return 0.0f;
    return (float)(k - 1) * 1e6f / (float)(t[0] - t[k - 1]);
}

// Called from feedbackPulse() (normal FALLING ISR) so edges are recorded in every mode.
void IRAM_ATTR benchRecordFalling(uint32_t now)
{
    if (benchActive) return; // bench mode has its own CHANGE ISR
    const uint32_t h = benchHead;
    benchTs[h & (BENCH_RING - 1)] = now & ~1u; // level 0 = falling
    benchHead = h + 1;
}

static void IRAM_ATTR benchEdgeIsr()
{
    const uint32_t now = micros();
    const uint32_t lvl = (REG_READ(GPIO_IN_REG) >> pinFeedback) & 1u;
    const uint32_t h = benchHead;
    benchTs[h & (BENCH_RING - 1)] = (now & ~1u) | lvl;
    benchHead = h + 1;
}

static void benchSetDuty(uint32_t d)
{
    if (d > PWM_DUTY_MAX) d = PWM_DUTY_MAX;
    if (railTripped || benchOverspeed) d = 0;
    benchDuty = d;
    setMotorDutyRaw(d);
    motorDutyApplied = (uint16_t)d;
    lastPwmFrac = (float)d / (float)PWM_DUTY_MAX;
    lastDriveV = lastRailCmdV * lastPwmFrac;
}

static void benchEnter()
{
    if (benchActive) return;
    autoFillStop("Stopped: bench mode");
    benchActive = true;       // control task yields from its next pass
    vTaskDelay(pdMS_TO_TICKS(20));
    benchSetDuty(0);
    if (boardHasVoltageControl)
    {
        setMotorRailVolts(RAIL_V_FLOOR);
        enableBuck();
    }
    detachInterrupt(digitalPinToInterrupt(pinFeedback));
    benchTail = benchHead;
    benchLost = 0;
    attachInterrupt(digitalPinToInterrupt(pinFeedback), benchEdgeIsr, CHANGE);
}

static void benchLeave()
{
    if (!benchActive) return;
    benchSetDuty(0);
    detachInterrupt(digitalPinToInterrupt(pinFeedback));
    attachInterrupt(digitalPinToInterrupt(pinFeedback), feedbackPulse, FALLING);
    if (boardHasVoltageControl)
        applyMotorDrive(0.0f);
    benchActive = false;
}

static void benchCommand(char *line)
{
    char *arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    if (strcmp(line, "ping") == 0)
    {
        Serial.println("OK ping");
    }
    else if (strcmp(line, "info") == 0)
    {
        String js;
        driveCalExportJson(js);
        js.replace("\n", "");
        js.replace("\r", "");
        Serial.printf("INFO cal %s\n", js.c_str());
        Serial.printf("INFO set maxSpeed=%u mph=%d pulsesPerKm=%.1f pwmNom=%.3f railMax=%.2f "
                      "fbEnable=%d fbDeadband=%.2f fbMinSpeed=%u trimGain=%.3f trimLimit=%.3f "
                      "speedTestOn=%d speedTestKph=%u calibrationModeOn=%d calibrationDriveV=%.3f sweep=%d "
                      "railFit=%d v0=%.3f k=%.3f maxHz=%.1f railMin=%.2f offset=%d\n",
                      maxSpeed, clusterInMph ? 1 : 0, sensorPulsesPerKm, pwmNominal,
                      railMaxV, feedbackEnable ? 1 : 0, feedbackDeadband, feedbackMinSpeed,
                      trimGain, trimLimit, speedTestOn ? 1 : 0, speedTestKph, calibrationModeOn ? 1 : 0,
                      calibrationDriveV, needleSweepOnBoot ? 1 : 0, railFitValid ? 1 : 0, railFitV0,
                      railFitSlope, benchMaxHz, railMinV, globalSpeedOffsetKph);
        Serial.println("OK info");
    }
    else if (strcmp(line, "speed") == 0 && arg)
    {
        // Run the REAL control loop (Speed Test) at this speed; bench mode must be off.
        if (benchActive)
        {
            Serial.println("ERR leave bench mode first");
            return;
        }
        if (benchOverspeed)
        {
            Serial.println("ERR overspeed latched - send clear");
            return;
        }
        speedTestKph = (uint16_t)atoi(arg);
        calibrationModeOn = false;
        speedTestOn = speedTestKph > 0;
        benchSpeedOwned = speedTestOn;
        Serial.printf("OK speed %u\n", speedTestKph);
    }
    else if (strcmp(line, "set") == 0 && arg)
    {
        // set <key> <value>: control-loop settings (RAM; the EEP task persists them)
        char *val = strchr(arg, ' ');
        if (!val) { Serial.println("ERR set key value"); return; }
        *val++ = 0;
        const float v = atof(val);
        if (strcmp(arg, "fbEnable") == 0) feedbackEnable = v != 0.0f;
        else if (strcmp(arg, "fbDeadband") == 0) feedbackDeadband = v;
        else if (strcmp(arg, "fbMinSpeed") == 0) feedbackMinSpeed = (uint16_t)v;
        else if (strcmp(arg, "trimGain") == 0) trimGain = v;
        else if (strcmp(arg, "trimLimit") == 0) trimLimit = v;
        else if (strcmp(arg, "railMin") == 0) railMinV = constrain(v, RAIL_V_FLOOR, (float)RAIL_HARD_MAX_V);
        else if (strcmp(arg, "railMax") == 0) railMaxV = constrain(v, RAIL_V_FLOOR, RAIL_V_CEIL);
        else if (strcmp(arg, "pwmNom") == 0) pwmNominal = constrain(v, 0.2f, 1.0f);
        else { Serial.println("ERR unknown key"); return; }
        resetDriveControl();
        Serial.printf("OK set %s %.4f\n", arg, v);
    }
    else if (strcmp(line, "calclr") == 0)
    {
        benchCalN = 0;
        Serial.println("OK calclr");
    }
    else if (strcmp(line, "calpt") == 0 && arg)
    {
        // calpt <speed> <hz> <drive> <rail> <pwm>: stage one mark
        DriveCalPoint p;
        unsigned sp = 0;
        if (benchCalN >= DRIVE_CAL_MAX ||
            sscanf(arg, "%u %f %f %f %f", &sp, &p.hz, &p.drive, &p.rail, &p.pwm) != 5)
        {
            Serial.println("ERR calpt");
            return;
        }
        p.speed = (uint16_t)sp;
        p.src = DCAL_SRC_EYE;
        p.rsv = 0;
        benchCal[benchCalN++] = p;
        Serial.printf("OK calpt %u\n", benchCalN);
    }
    else if (strcmp(line, "calcommit") == 0 && arg)
    {
        // calcommit <pwmNom> <railMin> <railMax>: replace the table through the normal
        // import path (same ordering checks as a file import)
        float pn = 0, rmin = 0, rmax = 0;
        if (sscanf(arg, "%f %f %f", &pn, &rmin, &rmax) != 3 || benchCalN == 0)
        {
            Serial.println("ERR calcommit");
            return;
        }
        JsonDocument doc;
        doc["type"] = DRIVE_CAL_FILE_TYPE;
        doc["unit"] = clusterInMph ? "mph" : "kmh";
        doc["pwmNominal"] = pn;
        doc["railMinV"] = rmin;
        doc["railMaxV"] = rmax;
        JsonArray marks = doc["marks"].to<JsonArray>();
        for (uint8_t i = 0; i < benchCalN; i++)
        {
            JsonObject m = marks.add<JsonObject>();
            m["speed"] = benchCal[i].speed;
            m["hz"] = benchCal[i].hz;
            m["drive"] = benchCal[i].drive;
            m["rail"] = benchCal[i].rail;
            m["pwm"] = benchCal[i].pwm;
        }
        String js;
        serializeJson(doc, js);
        const char *err = "";
        const bool ok = driveCalImportJson(js.c_str(), err);
        Serial.printf("%s calcommit %s (%u marks, railMin %.2f)\n", ok ? "OK" : "ERR", err,
                      driveCalCount, railMinV);
    }
    else if (strcmp(line, "calsrc") == 0 && arg)
    {
        // calsrc <speed> <0 eye|1 auto>: retag one mark (bench: mark inferred marks as auto)
        unsigned sp = 0, src = 0;
        if (sscanf(arg, "%u %u", &sp, &src) != 2) { Serial.println("ERR calsrc"); return; }
        for (uint8_t i = 0; i < driveCalCount; i++)
            if (driveCal[i].speed == sp)
            {
                driveCal[i].src = src ? DCAL_SRC_AUTO : DCAL_SRC_EYE;
                Serial.printf("OK calsrc %u %u\n", sp, src);
                return;
            }
        Serial.println("ERR calsrc no such mark");
    }
    else if (strcmp(line, "af") == 0 && arg)
    {
        // af 1: start Auto Fill (bench mode must be off); af 0: stop it
        if (atoi(arg))
        {
            if (benchActive) { Serial.println("ERR leave bench mode first"); return; }
            if (benchOverspeed) { Serial.println("ERR overspeed latched - send clear"); return; }
            const char *err = "";
            const bool ok = autoFillStart(err);
            benchAfOwned = ok;
            Serial.printf("%s af %s\n", ok ? "OK" : "ERR", err);
        }
        else
        {
            autoFillStop("Stopped (bench)");
            benchAfOwned = false;
            Serial.println("OK af 0");
        }
    }
    else if (strcmp(line, "afst") == 0)
    {
        Serial.printf("AF,%u,%u,%u,%u,%.2f,%.3f,%.2f,%s\n", (unsigned)autoFillState, autoFillIndex,
                      autoFillTotal, autoFillSpeed, autoFillTargetHz, calibrationDriveV, measuredFreqRawHz,
                      autoFillMsg);
    }
    else if (strcmp(line, "calrestore") == 0)
    {
        const char *err = "";
        const bool ok = driveCalRestoreBackup(err);
        Serial.printf("%s calrestore %s (%u marks)\n", ok ? "OK" : "ERR", err, driveCalCount);
    }
    else if (strcmp(line, "clear") == 0)
    {
        benchOverspeed = false;
        Serial.println("OK clear");
    }
    else if (strcmp(line, "tele") == 0 && arg)
    {
        benchTele = atoi(arg) != 0;
        Serial.printf("OK tele %d\n", benchTele ? 1 : 0);
    }
    else if (strcmp(line, "maxhz") == 0 && arg)
    {
        benchMaxHz = atof(arg);
        Serial.printf("OK maxhz %.1f\n", benchMaxHz);
    }
    else if (strcmp(line, "bench") == 0 && arg)
    {
        if (atoi(arg)) benchEnter(); else benchLeave();
        Serial.printf("OK bench %d cap=%.2f trip=%.2f\n", benchActive ? 1 : 0,
                      (float)RAIL_HARD_MAX_V, (float)RAIL_TRIP_V);
    }
    else if (!benchActive)
    {
        Serial.println("ERR not in bench mode");
    }
    else if (strcmp(line, "rail") == 0 && arg)
    {
        float v = atof(arg);
        if (v < RAIL_V_FLOOR) v = RAIL_V_FLOOR;
        if (v > RAIL_HARD_MAX_V) v = RAIL_HARD_MAX_V;
        setMotorRailVolts(v);
        lastDriveV = lastRailCmdV * lastPwmFrac;
        Serial.printf("OK rail %.3f\n", lastRailCmdV);
    }
    else if (strcmp(line, "duty") == 0 && arg)
    {
        long d = atol(arg);
        benchSetDuty(d < 0 ? 0 : (uint32_t)d);
        Serial.printf("OK duty %lu\n", (unsigned long)benchDuty);
    }
    else
    {
        Serial.println("ERR unknown");
    }
}

static void benchTask(void *)
{
    char line[128];
    uint8_t n = 0;
    uint32_t lastCmdMs = millis();
    uint32_t lastStatMs = 0;
    bool wdFired = false;
    for (;;)
    {
        while (Serial.available())
        {
            const int c = Serial.read();
            if (c == '\n' || c == '\r')
            {
                if (n)
                {
                    line[n] = 0;
                    n = 0;
                    lastCmdMs = millis();
                    wdFired = false;
                    benchCommand(line);
                }
            }
            else if (n < sizeof(line) - 1)
            {
                line[n++] = (char)c;
            }
        }

        // Overspeed trip, in EVERY mode (bench or the real control loop): motor off and
        // the bench takes the motor so the control task can't drive it again.
        const float hz = benchTachoHz();
        if (hz > benchMaxHz && !benchOverspeed)
        {
            benchOverspeed = true;
            benchSpeedOwned = false;
            speedTestOn = false;
            speedTestKph = 0;
            autoFillStop("Stopped: bench overspeed trip");
            if (!benchActive) benchEnter();
            benchSetDuty(0);
            Serial.printf("TRIP overspeed %.1f Hz > %.1f Hz - motor off (send 'clear')\n", hz, benchMaxHz);
        }

        if (!wdFired && (millis() - lastCmdMs) > BENCH_WATCHDOG_MS)
        {
            if (benchActive)
            {
                benchSetDuty(0);
                wdFired = true;
                Serial.println("WD no command - motor off");
            }
            else if (benchAfOwned && autoFillRunning())
            {
                autoFillStop("Stopped (bench watchdog)");
                benchAfOwned = false;
                wdFired = true;
                Serial.println("WD no command - auto fill stopped");
            }
            else if (benchSpeedOwned)
            {
                speedTestKph = 0;
                speedTestOn = false;
                benchSpeedOwned = false;
                wdFired = true;
                Serial.println("WD no command - speed test off");
            }
        }

        if (benchActive && boardHasVoltageControl)
            updateMotorRail(); // trip check every pass (RAIL_TRIP_V); the control task does it otherwise

        if (!benchActive && !benchTele)
        {
            benchTail = benchHead; // not streaming: drop recorded edges
        }
        else
        {
            // Drain the edge ring.
            uint32_t head = benchHead;
            if (head - benchTail > BENCH_RING)
            {
                benchLost += (head - benchTail) - BENCH_RING;
                benchTail = head - BENCH_RING;
            }
            while (benchTail != head)
            {
                char buf[300];
                int len = snprintf(buf, sizeof(buf), "E");
                uint8_t k = 0;
                while (benchTail != head && k < 24)
                {
                    len += snprintf(buf + len, sizeof(buf) - len, ",%lu",
                                    (unsigned long)benchTs[benchTail & (BENCH_RING - 1)]);
                    benchTail++;
                    k++;
                }
                Serial.println(buf);
            }

            const uint32_t now = millis();
            if (now - lastStatMs >= 100)
            {
                lastStatMs = now;
                Serial.printf("S,%lu,%.3f,%.3f,%lu,%lu,%lu,%d\n", (unsigned long)now,
                              lastRailCmdV, motorRailRawV,
                              (unsigned long)(benchActive ? benchDuty : motorDutyApplied),
                              (unsigned long)benchHead, (unsigned long)benchLost,
                              railTripped ? 1 : 0);
                if (!benchActive)
                {
                    // Control-loop telemetry: target speed/Hz, firmware Hz (filtered, raw),
                    // trim, drive, rail cmd, PWM fraction.
                    Serial.printf("T,%lu,%u,%.2f,%.2f,%.2f,%.4f,%.3f,%.3f,%.4f\n", (unsigned long)now,
                                  needleTargetSpeed, hzForSpeed((float)needleTargetSpeed), measuredFreqHz,
                                  measuredFreqRawHz, driveTrim, lastDriveV, lastRailCmdV, lastPwmFrac);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void benchInit()
{
    xTaskCreatePinnedToCore(benchTask, "bench", 4096, NULL, tskIDLE_PRIORITY + 2, NULL, 0);
    Serial.printf("BENCH build: rail cap %.2f V, trip %.2f V. Send 'bench 1'.\n",
                  (float)RAIL_HARD_MAX_V, (float)RAIL_TRIP_V);
}

#else
void benchInit() {}
#endif // BENCH_SERIAL
