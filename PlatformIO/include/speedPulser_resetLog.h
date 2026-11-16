#ifndef SPEEDPULSER_RESETLOG_H
#define SPEEDPULSER_RESETLOG_H

// SpeedPulser — start-up / reset-reason log (V5.02)
//
// Records WHY the ESP32-C3 started on every boot (power-on, brown-out, crash,
// watchdog, software restart...) in its own NVS namespace ("resetLog"), with a
// counter per cause and the last 10 starts. A start that never reached
// RL_STABLE_MS of running (lost power or reset again almost straight away) is
// counted as a "short run": several in a row point at the supply dropping out or
// bouncing while the car is started (e.g. powered from terminal 75 / X-contact).
//
// Two small NVS writes per boot (the new entry, then "ran OK" at 10 s), nothing
// else, so flash wear is negligible.
//
// Limitation: a chip that hangs in the boot ROM never runs this code, so that
// start can't be logged; it shows up as a Power-on the next time it's switched on.

#include <ArduinoJson.h>

void resetLogInit();                    // once, early in setup(): log this boot
void resetLogTick();                    // ~1 Hz from loop(): marks this boot "ran OK" after RL_STABLE_MS
void resetLogClear();                   // wipe the log; restarts with the current boot
void resetLogToJson(JsonDocument &doc); // for GET /api/resetlog

#endif // SPEEDPULSER_RESETLOG_H
