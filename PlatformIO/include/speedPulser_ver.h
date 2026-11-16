#ifndef SPEED_PULSER_VER_H
#define SPEED_PULSER_VER_H

#define FW_VERSION "5.09" // shared web UI with Pro, small audit fixes, tuning constants in defs.h
#define VERSION FW_VERSION  // legacy name used throughout this project

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
      - incoming hall speed now updates reliably: the speed input is sampled once per pulse instead of only when the frequency reading changed, so a steady signal still fills the median filter and the Incoming Speed read-out no longer sits frozen/blank.
      - the incoming frequency is now clamped to the configured Maximum Hall Frequency before mapping (and a zero setting is guarded), so an over-range or noisy signal reads as full scale instead of pegging the motor beyond the top of the calibration.
      - Speed Test / Calibration mode now fully lock out the vehicle hall input: enabling either mode clears any pending incoming reading (on top of the interrupt already ignoring new pulses), so a live signal can't disturb the test or linger on the display.

V4.00 - hardware voltage control (V4 PCB). The SAME firmware now runs on legacy and V4 boards: a 1k-to-GND strap on GPIO3 is read once at boot to auto-detect the V4 board (buck voltage control) vs a legacy PWM-only board, so old boards behave exactly as before.
      - V4 adds an adjustable motor supply: GPIO6 (V_ADJ) is a PWM->RC "DAC" injected into the TPS62933 feedback node (INVERSE: higher V_ADJ = lower motor volts) and GPIO7 enables the buck (driven HIGH after a safe minimum rail is set at boot).
      - closed-loop MID-RANGING control: because the gauge is linear (needle proportional to motor RPM), one top RPM point defines the whole scale. A fast PID drives the throttle PWM to hit target RPM while a slow integrator trims the buck voltage to keep the PWM near a nominal centre, so the voltage self-schedules to the motor's curve at every speed - no multi-point table.
      - simple top-speed calibration on V4: in Calibration mode the user lifts VOLTAGE and/or PWM until the needle pegs the top mark, then captures the measured RPM (feedbackMaxFreq) as the single anchor; the low cut-off reuses the existing feedback-min-speed. The legacy multi-point Calibration Builder is retained for old boards.
      - all voltage-control tuning (PWM nominal/min, voltage min/max, trim gain, inner PID gains) and the captured top-speed point persist in EEPROM.

V4.01 - standardised Forbes Automotive UI theme (shared style.css);
        adopted common wifi_manager (mDNS: can2rpm.local) and ota_manager
        (firmware + filesystem OTA via /api/ota, /api/ota/fs); per-product
        cache-busting on web assets
V4.02 - OTA overhaul (shared ota_manager / wifi_manager v2 + data/ota.js, ported
        from OpenHaldex 9.00): upload callbacks no longer answer mid-body (the
        old per-chunk "200 OK" made the browser drop the connection after the
        first 1.4 kB - a crash in AsyncTCP and a half-written partition, so no
        OTA through the UI had ever completed); filesystem updates unmount
        first, check the announced size, verify the mount and wipe on failure;
        boot only mounts a sane superblock and the web server always starts -
        with no usable UI "/" is a recovery page with the two uploads (replaces
        the old format-on-fail + "LittleFS not available" root).
        "Update from GitHub" on the OTA tab (Releases/releases.json via
        tools/make_release.py) plus a Home WiFi (bridge mode) card; power_manager
        holds WiFi up while any browser is active. Assets served no-cache (ETag)
        instead of the hand-bumped ?v=.

V5.00 - V5 PCB (PCBs/rev6-allConfirmed) support: voltage feedback + simple calibration.
      - GPIO map confirmed against the rev6 netlist: GPIO1 V_MOT_SENSE (new), GPIO2 motor,
        GPIO3 R22 1k board strap, GPIO4 feedback, GPIO5 speed in, GPIO6 V_ADJ, GPIO7 EN_ADJ,
        GPIO10 direction. Board detection stays the passive GPIO3 strap read at boot.
      - motor-rail read-back: GPIO1 (ADC1, R24 39k / R25 10k) read every 100 ms and shown
        as the measured rail on the Advanced and Calibration pages.
      - "Test Board" (Advanced page): with the motor stopped, steps the rail to 5 / 7.5 / 10 V
        and reads each back, proving the V_ADJ injection AND the sense divider. Pass/fail
        and readings are stored; a pass also stores this board's own duty->volts line, so
        rail commands land on the volts asked for.
      - rail commands are now in VOLTS across 3.0-12.0 V (the 0..1 command spans that
        window). The rail ceiling is 12 V (was 0 % V_ADJ = ~13.8 V / dropout); with voltage
        control off the rail holds 9 V, the old fixed supply the duty presets were
        captured on.
      - voltage-board calibration simplified: Calibration Mode holds the PWM at Nominal PWM
        (default now 80 %) and the user only raises the RAIL (-/+ volt buttons) until the
        needle sits on a top mark, enters the speed shown, and captures. The tacho Hz is
        averaged over ~1 s and refused while still settling. The point (speed, Hz, rail,
        PWM) is stored in cluster units; every lower speed is linear to zero
        (needle ∝ RPM), and the captured rail seeds the voltage feed-forward.
      - the legacy multi-point Calibration Builder is unchanged and still the only
        calibration shown on boards without voltage injection.
      - calibration rail now spans the full 3-12 V regardless of Minimum/Maximum Voltage
        (those only bound the running loop), and the legacy 9 V hold ignores them too.
        A top point captured below Minimum Voltage drops Minimum Voltage to 3 V so the
        running loop can follow it. The rail slider sends one request at a time (latest
        value wins) instead of one per input event, so it no longer snaps back.

V5.01 - drive calibration: the voltage-board control and calibration reworked from bench testing.
      - one DRIVE value (V = rail x PWM) commands the motor. Above Minimum Rail x Nominal PWM
        the PWM holds at Nominal and only the rail moves; below it the rail holds at Minimum
        Rail and the PWM falls to zero. The slowest speeds are reachable at last (V5.00 could
        not go below ~4.4 V at 80 % PWM, which was miles too fast on some clusters).
      - multi-mark calibration replaces the single top point: in Calibration Mode set the drive
        until the needle sits on a mark, capture it; repeat for a low mark, the top and a few
        between (up to 10). Each mark stores speed, tacho Hz, drive, rail and PWM. Running
        interpolates the drive between marks (open loop), and Measured Speed maps tacho Hz back
        through the same marks, so a dial that isn't linear near zero reads right (V5.00 showed
        16 km/h with the needle on 20). The V5.00 top point is carried over as the first mark.
      - the mid-ranging law (fast PWM PID + slow voltage loop) is gone: it fought itself and
        ignored Feedback Enable. Feedback is now one slow trim on the calibrated drive, only with
        Feedback Enable on, above Min Feedback Speed and outside the PID Deadband, capped at
        ±Feedback Trim Limit (default 20 %).
      - tacho and rail are measured every 100 ms in every mode, and Speed Test's status now
        carries the rail/drive/Hz fields: the read-outs no longer freeze outside Calibration.
      - calibration card: drive -/+ buttons and slider sit above every read-out, the hint line
        has a reserved height and the values can't wrap, so nothing moves the buttons.
      - Voltage Control settings are now Nominal PWM, Maximum Rail (V), Feedback Trim
        Gain and Limit; the old 0..1 Min/Max Voltage, Minimum PWM and PWM-loop Kp/Ki/Kd are gone.
      - Speed Test on the voltage board no longer writes a legacy duty when the speed slider moves,
        and the needle sweep drives up to the calibrated Max Speed instead of raw PWM at boot rail.
      - own build folder (speedpulser-v501-pio) so the V5.00 build stays intact.
      - capture "Steady" now tests DRIFT (newer half-second mean vs older, <= 2 % or 0.75 Hz),
        not spread: low-speed motor cogging (e.g. 23.1-24.7 Hz) is a wobble around a constant
        mean and averages out, so it no longer blocks a capture. Spread only has to stay under
        20 % / 4 Hz, which rejects a stalling or stuttering motor.
      - running speed is capped at the top calibration mark: no extrapolated drive / trim target
        past it, so a high Max Speed or input can't overspin the motor.
      - Minimum Rail is fixed at 3.0 V (the hardware floor; setting removed).
      - Reverse Direction inverted: unticked (pin LOW) is now the correct way. Stored under a new
        EEP key, so units that had Reverse ticked to compensate come up unticked = correct.
      - dashboard graph legend coloured again (the swatch styles were lost in the V4.02 theme),
        with solid/dashed/dot swatches matching each trace and the axis each is read against.
      - voltage calibration export/import: Export Voltage Cal saves the marks + Nominal PWM / Max
        Rail; files are tagged "type": speedpulser-voltage-cal or speedpulser-pwm-cal and the one
        Import button loads whichever it is. Cal POST bodies are now collected across TCP chunks.
      - "Cal:" banner reads "Voltage (n marks, lo-hi)" when Voltage Control is driving the motor,
        and the preset selector notes that the PWM preset is idle.

V5.02 - start-up log, to diagnose units that misbehave when the car is started (needle pegged /
        no WiFi after cranking): every boot records WHY the ESP32 started (power-on, brown-out,
        power glitch, crash, watchdog, software restart, USB/reset pin) in its own NVS namespace
        ("resetLog"), with a count per cause and the last 10 starts.
      - a start that never reaches 10 s of running is counted as a "Short Run": several in a row
        mean the supply is dropping out or bouncing during cranking (e.g. wired to terminal 75 /
        X-contact instead of 15).
      - Advanced page "Start-up Log" card (this start's cause, counts, history, Refresh / Clear Log);
        GET /api/resetlog, POST /api/action {"action":"clearResetLog"}.
      - two small NVS writes per boot only. A chip that hangs in the boot ROM can't be logged; it
        shows as a Power-on at the next switch-on.

V5.03 - calibration drive can no longer be dragged: the Drive slider on the Calibration card is now
        display-only (shows the drive, can't be touched) and the -/+ buttons are the only way to change
        it. A stray tap on the slider jumped the motor voltage and nearly over-drove a speedo.
      - firmware guard: one tempDriveV request may raise the calibration drive by at most 0.5 V (the
        largest button step); lowering is unrestricted. Covers a cached old page or a second browser.

V5.04 - bench session on a TY3816B fitted to a cluster (2026-11-15, tools/bench_tacho.py + env:bench):
      - tacho measured from pulse TIMING: the ISR keeps the last 32 edge times and the reading
        averages whole revolutions (5 pulses per turn: the pole spacing isn't even and a whole
        turn cancels it) over >= 200 ms. V5.03 counted pulses per 100 ms window and needed two,
        so below ~20 Hz windows read 0 while the motor was still turning (20 -> 0 -> 20; at 8.5 Hz
        every window read 0). Holds steady down to the motor's floor (~6 Hz); between pulses it
        holds but reads down once a pulse is overdue (no faster than 1.5 / time since the last
        pulse); 0 after 0.6 s. Edges < 1 ms apart are dropped (line noise pairs seen on the bench).
      - Minimum Rail is a setting again (Advanced > Voltage Control), default 4.8 V. The motor's
        driver needs ~4.75 V+: from 3.1 to 4.6 V low speeds jump, stall and won't restart; at 4.8 V
        the PWM alone runs it 2 % (8 Hz) to 78 % (140 Hz) near-linearly and it started from rest
        47/47 times down to 1.7 %. A unit updated with a calibration keeps 3.0 V (its marks were
        captured with the rail on the floor; at 4.8 V the same drive runs about twice as fast)
        until the marks are recaptured. Cal files carry railMinV; older files import as 3.0 V.
      - motor off idles the rail at the 3.0 V floor (not at Minimum Rail).
      - feedback trim compares the tacho with a reference model (the target followed with the
        motor's own lag: 63 % in ~0.35 s) and only integrates once the model has reached the
        target. Integrating the raw step error wound the trim up mid-acceleration: 5-31 Hz
        overshoot on a speed-up, and a big slow-down dipped the needle to ~0 for ~7 s.
      - the trimmed drive is capped at the top mark's drive + 5 % (no overspin past the dial).
      - bench-tuned feedback defaults on voltage boards (new units / Reset PID Defaults):
        Feedback Enable ON, Trim Gain 2.0, Trim Limit +/-40 %, Deadband 0.3 Hz, Min Feedback
        Speed 8. Held 10-160 km/h within ~1 % of the captured tacho speed, recovered a sudden
        27 % slow-down at 20 km/h in ~1.5 s. Legacy boards keep the PID-path defaults.
      - env:bench (not for release): serial bench interface for tools/bench_tacho.py, rail hard-
        capped at 4.8 V, trip at 5.0 V read-back, firmware overspeed trip, Board Test and the
        boot needle sweep disabled. tools/flash.sh flashes with esptool directly.

V5.05 - Drive Calibration Auto Fill (speedPulser_autoFill.cpp). A calibration is two curves: the
        dial (tacho Hz -> needle), which only a person can see but is a straight line (bench: 10
        eye marks 40-193 km/h within 0.9 km/h of one line; 40 + 140 alone predict the rest within
        1.4 km/h), and the motor (drive -> Hz), which the tacho measures. The user captures the
        top mark by eye (optionally a low one too); Auto Fill keeps every eye mark's speed + Hz,
        adds auto marks at 10/20/30/40 then every 10 to the top (wider if the table runs out; none
        below 7 Hz) on the line through the eye marks, and for each mark, lowest first, finds the
        drive that holds its Hz (secant steps, settle 1.2-1.6 s, average 1-2 s, within
        max(0.15 Hz, 0.5 %)). Never faster than the top mark (raw tacho > topHz x 1.06 + 1 Hz
        stops it), drive <= 1.25 x the top mark's capture drive and the Max Rail limit, steps up
        <= 25 %. Fails safe: motor off, table untouched. Runs in Calibration Mode; turning that
        off, Speed Test or Stop ends it.
      - marks carry their source: eye (captured) or auto (Auto Fill), in what was struct padding
        (NVS "dcSrcV" marks the new layout; older tables read as all eye). Cal files carry "src".
        Capturing an eye mark drops the auto marks at that speed or out of order with it, and
        makes room by dropping the nearest auto mark when the table is full.
      - up to 20 marks (was 10).
      - one backup table in NVS ("dcBak", an export file incl. drive settings), saved just
        before Auto Fill replaces the table. Restore Previous swaps the two.
      - API: actions calAutoFill / calAutoFillStop / calRestore; status "autoFill" {state, index,
        total, speed, hz, msg} and "calBackup". Bench: af 1|0, afst, calsrc, calrestore.

V5.06 - audit pass (2026-11-16).
      Safety:
      - an uncalibrated voltage board no longer drives the motor outside Calibration Mode. The old
        guess ran a straight line up to Max Rail x Nominal PWM (9.6 V with the defaults) at Max
        Speed, so the first Speed Test on a new board pegged the speedo.
      - needle sweep: refused unless the car is stopped and no test is running (it holds the control
        task for seconds); Sweep Speed is at least 1 (0 jumped the needle to the top in one step);
        on a voltage board it sweeps to the top calibration mark (no marks = no sweep). The legacy
        board still sweeps to full PWM (to be reviewed).
      - Calibration Builder refuses a point whose duty doesn't rise with speed, and interpolates in
        signed maths: a falling pair used to wrap to a duty in the millions. Imports are checked as
        a whole first.
      - every /api/control value is range-checked (a bad PID gain, Max Hall 0, Min Rail above Max
        Rail could each over-drive or kill the needle); the reply carries the value kept.
      - drive marks can only be changed, cleared, restored or imported in Calibration Mode, and the
        direction only with the motor stopped (no needle jumps while driving).
      - Speed Test is never saved or loaded (a badly timed save could boot a unit into Speed Test
        at 100 km/h), and settings ARE saved while Speed Test runs (they were lost before).
      - calibration tables are saved table first, count second, and the count is taken from the
        table's size: a power cut between the two writes used to lose every mark.
      - web POSTs must be application/json (a page on another site can't drive the motor) and are
        collected across TCP chunks. OTA only with the car stopped and no test running; the motor
        stops when an upload starts; a firmware upload whose phone disconnects is aborted; a web UI
        upload cut off by power loss is caught at the next boot (recovery page).
      Speed input (speedPulser_control.cpp):
      - the sensor is ONE setting, pulses per km (VW / Audi 3600 = 1 Hz per km/h; 4000 / 8000 /
        16000 pulses-per-mile presets). Max Speed is only the needle limit now. Old units carry their
        Max Hall Frequency / Max Speed ratio over.
      - glitch filter = half a period at Max Speed (200 us - 3 ms) instead of a fixed 3 ms, so
        sensors over 333 Hz no longer read half speed.
      - the speed is timed pulse by pulse and averaged over the Smoothing time (default 200 ms) on a
        100 ms tick, in floating point. The old 1 ms "window" held one pulse, then a block median of
        N pulses updated the needle in steps (0.6 s steps at 10 km/h). RunningMedian is gone.
      - offset curve interpolated between 25 / 75 / 125 / 175 / 225 km/h (it stepped at the band
        edges); the global offset is one signed value; offsets only apply while moving.
      - km/h -> mph rounds instead of truncating (the needle read ~0.5 mph low on average).
      Removed (not used by any board in the field):
      - "Voltage Control off" (fixed 9 V rail + presets on a voltage board).
      - the legacy PID (Kp/Ki/Kd, feedbackMaxFreq, Capture Top Speed): legacy boards have no
        motor feedback wired. Voltage-board feedback is unchanged.
      - dead code: 23 no-op Preferences.begin() calls (every setting is in the namespace
        "hasNeedleSweep", kept), the never-run first-boot block, unused debug macros, speedMultiplier,
        updateMotorPerformance, calPrevious / calNext, the diag task; the control task is one
        100 ms tick (was a 1 ms loop).
      Web UI: rebuilt around the setup order. Dashboard / Setup (1 Board, 2 Cluster & Speed Sensor,
        3 Calibrate, 4 Speed Test, 5 Offset, 6 Needle Sweep) / Advanced / Diagnostics / Update.
        One control per setting, board-specific cards only on their board, one API helper that shows
        the unit's errors and puts refused values back, status polled every 250 ms (paused when the
        tab is hidden), the reload no longer re-sends the preset, MPH change confirmed when marks
        exist, units shown correctly with an mph dial, stylesheet trimmed to what the UI uses.
      Names: dutyCycle -> inputSpeedKph, dutyCycleIncoming -> inputFrequencyHz, requestedSpeed ->
        needleTargetSpeed, testSpeedo / tempSpeed -> speedTestOn / speedTestKph, testCal ->
        calibrationModeOn, motorPerformanceVal -> presetId, vc* -> pwmNominal / railMinV / ... (NVS
        keys unchanged, so every saved setting loads).

V5.07 - even needle sweep (2026-11-16).
      - the sweep ramps the dial SPEED evenly and looks up the drive / duty for it through the
        calibration every 10 ms. It used to ramp the drive (voltage board) or the raw duty (legacy)
        evenly; the motor's speed isn't a straight line against either, so the needle leapt off the
        stop, slowed in the middle and rushed at the end.
      - it runs from the lowest to the highest calibrated speed at Sweep pace (ms per dial km/h or
        mph), slows to a stop over the last 20 % of the climb, then waits at the top: with a tacho
        until the motor is within 3 % of the top mark (at most 1.5 s), without one 0.4 s. The way
        down is the climb backwards; at the lowest speed the motor is cut and coasts home.
      - legacy board: now sweeps to the top of the selected preset / custom table, not to full PWM
        against the stop. No calibration = no sweep, on either board.
      - the rail over-voltage check and the tacho keep running during the sweep.
      - driveForSpeed() takes a fractional speed; sweep constants in speedPulser_defs.h.

V5.08 - second audit pass (2026-11-16).
      Calibration:
      - legacy: Calibration Builder edits and Export no longer overwrite the running preset table
        (the needle could go dead, or run the custom curve under the preset's name, until a
        reboot). A selected custom cal with < 2 points falls back to preset 1.
      - legacy: Save & Use (was Generate & Apply + Save to Device) always saves the points;
        picking Custom saves them too. Apply alone used to lose them at the next ignition-on.
      - legacy: Builder point changes need Calibration Mode; mph can't be switched away from the
        unit a selected custom cal was captured in.
      - Auto Fill clears the auto marks at the start and adds each mark as it is measured (the
        table builds up on screen); a stopped or failed run puts the old table back. Stop is
        handled in the control task (a stop could be undone by the running tick). It refuses to
        start with the car moving.
      - a mark's eye / auto pill can be tapped to switch it (pre-V5.05 tables load all "eye").
      - Minimum Rail / Nominal PWM / Maximum Rail change in Calibration Mode only. Maximum Rail
        default 8 V for a new unit (was 12 V); up to 12 V allowed.
      Safety:
      - Speed Test and Calibration Mode end when WiFi goes to sleep (nothing could stop them).
      - feedback: a tacho that goes silent for 1.5 s drops the trim (open loop) and the page says
        so; readings far from the model are never integrated (a broken wire wound it to +40 %).
      - rail over-voltage trip in every build: 2 V over the command for 0.5 s (command steady
        3 s) turns the motor and buck off until a restart.
      - Speed Test is capped at Maximum speed.
      - OTA / Home WiFi POSTs from another site's page are refused (Origin check); a web UI
        upload is checked once at the start (a speed blip mid-upload wiped the UI).
      - the home WiFi retry no longer switches the radio back on in low-power mode.
      Web UI:
      - tests are orange: badge, a Dashboard banner with Stop, an orange line under the header.
      - an emptied number box puts its value back (it sent 0: 100 pulses/km pinned the needle);
        a refused or clamped value shows in the box being edited; "Offline" after ~1 s with no
        answer, and the page keeps retrying the first load.
      - phone sliders only move on a sideways drag (a tap or a scroll no longer changes them).
      - Calibration Mode controls greyed out until it's ticked; dashboard tiles centred; tabs on
        one line on a phone (Adv. / Diag. / OTA); mph tick next to Reverse; helper text cut down.
      - guided OTA downloads both files before flashing anything; preset names shown as text.

V5.09 - shared web UI + small audit fixes (2026-11-16).
      Shared (both projects):
      - one app.js for SpeedPulser and SpeedPulser Pro: the web API uses the same names in both
        (Pro renamed e.g. testCal -> calibrationModeOn, tempSpeed -> speedTestKph, vcRailMaxV ->
        railMaxV, convertToMPH -> clusterInMph; saved settings are untouched). /api/control replies
        {"status":"ok","value":...} on both; an unknown setting or action is refused.
      - tuning constants (Auto Fill, tacho, feedback trim, input glitch limits) moved out of the
        .cpp files into one Tuning section of the main header.
      - micros() wrap: a parked unit forgets old pulses (a twitch every 71.6 min); slow sensors get a
        longer stop timeout (up to 5 s) instead of flickering to 0.
      - imported and stored drive tables are range-checked (drive <= 12 V, Hz, rail, PWM, order);
        tables save their count first, so a power cut can't leave 0 marks over a new table; the
        settings save takes a consistent copy of the table (it could catch one mid-change).
      - Test Board never steps past Maximum rail, and needs Maximum rail >= 6 V.
      - the Calibration Builder is refused on a voltage board; a PWM file can't be imported there.
      - a POST with no body is answered (it hung); mark speeds are checked before they wrap.
      - the board-ID pull-up is turned off after reading (~72 uA); preset tables are const
        (14 KB of RAM back); retired NVS keys removed once replaced; library versions pinned;
        unused code and status values removed.
      - page: "Motor speed" dial only on boards with a tacho; Test Board shows "No answer" if the
        unit stops answering mid-test.
      - feedback anti-windup: the trim stops growing when the drive hits its cap (1.05 x the top
        mark, or Max Rail x 100 %); it used to wind up to the +40 % limit against the cap. The page
        says when the trim sits at its limit (the calibration doesn't match the motor there).
      - phone: a touch outside the box being typed in releases it (each slider move scrolled the
        page back to the last box typed in); the Auto Fill prompt lists the eye marks it keeps.

Notes:
- Inputs are a 5v/12v square wave input from Can2Cluster or an OEM Hall Sensor
- Converts to PWM signal for a BLDC motor
- Motor voltage reduced via adjustable LM2596S on the PCB from 12v to ~9v
      - *V2 changes this to two buck converters: one for the motor, one for the ESP32
- Allows <10mph readings while supporting high (160mph) readings
- Default support for 12v hall sensors from 02J / 02M etc: 1Hz = 1km/h
- Uses 'RunningMedian' for capturing multiple input pulses
      - page: the motor's FG signal is called "Motor feedback" (Diagnostics tile, feedback-lost warning), not "tacho".
*/

#endif  // SPEED_PULSER_VER_H
