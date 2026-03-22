#ifndef SPEED_PULSER_VER_H
#define SPEED_PULSER_VER_H

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

Notes:
- Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor
- Converts to PWM signal for a BLDC motor
- Motor voltage reduced via adjustable LM2596S on the PCB from 12v to ~9v
- Allows <10mph readings while supporting high (160mph) readings
- Default support for 12v hall sensors from 02J / 02M etc: 1Hz = 1km/h
- Uses 'ESP32_FastPWM' for easier PWM control compared to LEDc
- Uses 'RunningMedian' for capturing multiple input pulses
- ESP32 C3 is the primary target board (LOLIN C3)
*/

#define VERSION "1.19"

#endif  // SPEED_PULSER_VER_H
