#ifndef SPEED_PULSER_VER_H
#define SPEED_PULSER_VER_H

#define VERSION "3.11"

/*
SpeedPulser - Forbes Automotive '25
Analog speed converter suitable for mechanical drive speedometers. Tested on VW, Ford & Fiat clusters.

Version History:

V1.01 - initial release
V1.02 - added onboard LED pulse to confirm incoming pulses
V1.03 - added Fiat cluster - currently not working <40mph due to 'stickyness' of the cluster and motor not having enough bottom end torque.
      - set to 110mph max (with lower motor voltage), allows full range calibration between 20kmph and 180kmh.  Any more and you're speeding anyway...
V1.04 - added Ford cluster - actually much more linear compared to the VW one!
V1.05 - added 'Global Speed Offset' to allow for motors installed with slight binding.  Will keep the plotted duty/speed curve but offset the WHOLE thing
V1.06 - added 'durationReset' - to reset the motor/duty to 0 after xx ms.  This means when there is a break in pulses (either electrical issue or actually stopped, reset the motor)
V1.07 - added in 160mph clusters for MK2 Golfs (thanks to Charlie for calibration data!)
V1.08 - missed reset dutyCycle to 0 after needle sweep - causes needle to hang slight above zero.
V1.09 - added Martin Springell's MK1 Golf Calibration
V1.10 - added speed multiplier
V1.11 - added WiFi settings page - saves remote coding
V1.12 - added Merc W123 calibration and threaded model to suit
V1.13 - added calibration selection in WiFi.  Removed Minimum Speed & Minimum Hall - assumed zero
V1.14 - added 'cannot find' speed to set to zero and low speed issues
V1.15 - added 'calibration mode' - to be able to go through with buttons on WiFi to make cal easier.
V1.16 - Sam Wade provided a 160mph cluster - speed corrected to suit.  New array(!).  Calibration selection incorrect and shifted '1' down!
V1.17 - added 70mph Smiths cluster - uses a 5/8 18tpi housing
V1.18 - added Over-the-Air updates
V1.19 - added 90mph Smiths cluster

V2.00 - ported to PlatformIO
V2.10 - Changed to PWM LEDc support
V2.20 - added Cluster in MPH
V2.21 - fixed LEDC motor output for Arduino-ESP32 3.x: ledcWrite() is now pin-based and ignores channels created via ledc_channel_config(). Replaced all motor ledcWrite() calls with native ledc_set_duty()/ledc_update_duty() via setMotorDuty().

V3.00 - PCB revision to include Motor Feedback + Reverse option and two Buck Converters (one for the motor, one for the ESP32). 
      - closed-loop (PID) calibration + Calibration Builder (set duty, capture per-speed points, generate/apply/save, export/import).
      - motor feedback frequency defined as a global (254 Hz) after bench measurement - other motors may need this changed.
      - UI overhaul: Signal Filter + speed-offset curve moved to Configuration; Status Monitor shows measured speed, PID trim; cal mode auto-disables PID while active.

V3.01 - added a live calibration-curve graph on the Dashboard (duty-vs-speed trace + captured points + marker for the point currently achieved from hall input, Speed Test or Calibration Mode).
      - graph now scales to the duty range actually used, labels the axis and marker in raw duty (matching the Motor Duty gauge), and redraws when the calibration is changed.
      - Calibration Builder duty change now rolls over and under so the final full-scale point is quick to reach.
      - Export/Import simplified to plain text files; Export saves a .txt and Import opens a file picker directly (no manual pasting).

V3.02 - smoother needle sweep: the sweep now ramps the DISPLAYED speed linearly over time and looks up its duty each 10 ms tick (mirrors the SpeedPulser Pro linearSpeedSweep) instead of a straight duty ramp, so the needle moves at a constant rate.
      - Sweep Speed (ms) slider is now live: dragging it re-times an in-progress sweep on the fly (progress is integrated from the live rate each tick, so it retimes smoothly without jumping).

V3.03 - fixed the live calibration-curve marker sitting off the curve: it now plots the mapped road speed (after offset/multiplier/MPH conversion) against the applied duty, instead of the raw hall input frequency. With feedback off the marker sits on the curve; with feedback on it reflects the PID-trimmed duty as intended.

V3.04 - needle sweep now reaches full deflection again: it ramps to the active calibration's top achievable speed (so it drives to the cal's maximum duty), instead of the maxSpeed dial value which left the needle short whenever the dial was set below the calibration's top speed.

V3.05 - needle sweep now drives the FULL 12-bit PWM range (0..PWM_DUTY_MAX) so the needle reaches its mechanical full deflection. The 10-bit presets only reach 384<<2 (~37% of range), which left the needle short on the 12-bit hardware; the sweep is a physical self-test to the peg, so it ramps the whole duty range rather than a calibration's top-speed duty.

V3.06 - OTA + /api/version ported to the shared `ota_manager` module (same one used by SpeedPulser Pro): identical endpoints/behaviour, but the post-flash reboot now runs from a short FreeRTOS task instead of a blocking delay() in the request handler, so the async web server can flush the success response before the device restarts.

V3.07 - custom calibrations now remember the cluster unit they were captured in (metadata already stored as "mph"/"kmh"), and the device auto-enables "Cluster in MPH" whenever an MPH cal becomes the active calibration (boot, dropdown selection, apply/save/import). The web UI mirrors the change: /api/cal now returns convertToMPH so the checkbox and speed-unit labels update automatically.

V3.08 - added a "Cluster in MPH" toggle directly on the Calibration Builder page so the capture unit is set/seen where it matters. It stays in lock-step with the Configuration page toggle (either one drives the device setting and both, plus the capture unit label, update together).

V3.09 - motor feedback pin is now monitored for availability: legacy PCBs without the feedback trace are detected automatically. Measured Speed / PID Trim stay "--" until a real tacho signal is seen (never forced to 0 on boot), show "N/A" if the motor runs with no feedback present, and the PID closed loop only engages once feedback is actually available (so enabling it on a board without feedback safely stays open-loop).
      - fixed vehicle hall input not being ignored during Speed Test / Calibration: incoming pulses are now dropped at the interrupt while a test or cal is active, so the motor is driven solely by the test controls until the test is turned off.

V3.10 - tightened closed-loop accuracy: the PID deadband no longer freezes the integrator, so the loop trims out the last couple of Hz and settles on the target instead of sitting ~2 km/h off. Inside the deadband only P and D are silenced for anti-hunt; the integral keeps nulling the steady-state error.
      - added a user-configurable "PID Deadband (Hz)" slider (0-5 Hz, persisted); 0 = always full PID.
      - added the "Export C-Array" button to the Calibration Builder: it downloads a paste-ready firmware preset (a motorPerformanceN[] table plus its calibrationProfiles[] line) so a calibration captured on a device can be built in as a permanent, selectable preset in new firmware. The array is auto-numbered after the last built-in preset so it drops straight in.

Notes:
- Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor
- Converts to PWM signal for a BLDC motor
- Motor voltage reduced via adjustable LM2596S on the PCB from 12v to ~9v
      - *V2 changes this to two buck converters: one for the motor, one for the ESP32
- Allows <10mph readings while supporting high (160mph) readings
- Default support for 12v hall sensors from 02J / 02M etc: 1Hz = 1km/h
- Uses 'RunningMedian' for capturing multiple input pulses
*/

#endif  // SPEED_PULSER_VER_H
