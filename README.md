# SpeedPulser

The SpeedPulser converts digital speed pulses from a gearbox hall sensor (or a [Can2Cluster](https://forbes-automotive.com) signal) into a 10 kHz PWM signal that drives a BLDC motor — giving life back to an OEM analog speedometer without a mechanical cable. It is fully open-source, WiFi-configurable, and calibrated per cluster type so the needle reads accurately across the available speed range of the motor.

It is based on a **LOLIN C3 Mini (ESP32-C3)** and uses a **TY3816B** BLDC motor driven via a native LEDC hardware PWM channel.

![SpeedPulser Web UI — dashboard, configuration, limits & offset, PID feedback, status monitor, speed test, top-speed calibration and OTA](/Images/speedPulserUI.png)

![SpeedPulser Web UI — voltage control, calibration builder and export / import](/Images/speedPulserUI-2.png)

---

## Features at a Glance

| Feature | Detail |
|---|---|
| Speed input | 5V / 12V square-wave (hall sensor or Can2Cluster) |
| Motor output | 10 kHz hardware PWM, 12-bit resolution |
| Closed-loop feedback | PID trims duty from the motor feedback pin (optional) |
| Calibration profiles | 18 built-in (VW, Ford, Fiat, Merc, Smiths, Opel, VW Bay) |
| Custom calibration | Calibration Builder — capture, generate, save, export/import |
| Calibration Curve | Live duty-vs-speed curve with the current operating point on the Dashboard |
| Status Monitor | Live measured speed, PID trim and actual motor speed (Hz) |
| WiFi UI | Web app type interface |
| Needle sweep | Configurable on power-up |
| Speed offset | Global fixed offset **or** 5-point speed-dependent curve |
| OTA updates | Firmware upload from the browser |
| Power management | Auto WiFi-off + CPU scaling after 1 min idle |
| Remembers settings | All settings stored to ESP32 Preferences (NVS/EEPROM) |

---

## Supported Models

| Cluster | Range | Calibration by |
|---|---|---|
| VW MK1 / MK2 Golf | 120 mph | Martin Springell |
| VW MK1 / MK2 Golf | 120 mph | Forbes Automotive |
| VW MK1 / MK2 Golf | 120 mph | Tara |
| VW MK1 / MK2 Golf | 140 mph | Forbes Automotive |
| VW MK1 / MK2 Golf | 160 mph | Forbes Automotive |
| VW MK1 / MK2 Golf | 300 kph (1.8T) | Forbes Automotive |
| VW Bay VDO | 90 mph | Community |
| Ford Escort | 120 mph (var. 1) | Forbes Automotive |
| Ford Escort | 120 mph (var. 2 — Darren) | Community |
| FIAT Uno | 40–160 mph | Forbes Automotive |
| FIAT Uno | 20–110 mph | Forbes Automotive |
| Mercedes W123 | 120 mph | Forbes Automotive |
| Smiths 5/8" | 70 mph | Forbes Automotive |
| Smiths 5/8" | 90 mph | Forbes Automotive |
| Smiths 5/8" | 150 mph | Forbes Automotive |
| Opel Manta A-72 | 200 km/h | Forbes Automotive |

> Users are actively encouraged to submit new models, calibrations, or corrections via GitHub or Discord.

---

## Purchase

Pre-assembled SpeedPulser units are available here: [SpeedPulser — Forbes Automotive](https://forbes-automotive.com/products/speedpulser)

---

## More Detailed Instructions

The PDF Installation Guide on GitHub provides full step-by-step hardware fitting instructions.

---

## Hardware Overview

### PCB

The pre-assembled PCB:

![SpeedPulser Board Overview](/Images/BoardOverview.png)

---

### Power Supply & Hall Sensor Input

The three-pin **Input** connector on the bottom edge of the PCB accepts 12V battery power and the speed signal:

![PCB Input & Motor Connectors](/Images/PCBConnectors.png)

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | PWR_IN (12 V) | Closest to board edge; connect to ignition-switched 12 V |
| 2 | GND | Common ground |
| 3 | Speed Signal | 5 V or 12 V square wave from hall sensor or Can2Cluster |

> **Pull-up / Pull-down jumper:** Different hall sensors require either a pull-up or pull-down resistor. A 2-way jumper header on the PCB selects this. If the SpeedPulser is not registering incoming pulses, swap the jumper position. Sensors with an internal resistor can have the jumper removed entirely.

An on-board adjustable **LM2596S** buck converter steps the 12V supply down to approximately 9V to power the motor, keeping the motor's operating range and torque within spec across the full speed scale.

---

### Motor Connector

The five-pin **Motor** connector on the top edge of the PCB:

| Pin | Signal | Notes |
|-----|--------|-------|
| 1 | Motor Power | Black — 5–9 V (set via on-board trimmer) |
| 2 | Motor Feedback | White — BLDC feedback pulse; read on GPIO 4 for closed-loop PID (optional) |
| 3 | Motor Direction | Green — LOW (normal); enable Reverse to flip needle direction — HIGH (reversed) |
| 4 | Motor Ground | White — motor ground |
| 5 | Motor PWM | 10 kHz PWM from ESP32 (via NPN level-shifter to 4V) |

---

### Coupler Installation

Motors are supplied with the coupler pre-fitted; the final drive pin and securing pins are included separately.

1. Remove the motor from the coupler housing.
2. Align the drive pin into the coupler recess and press home — a few light taps should seat it fully.
3. A spare coupler is supplied. Couplers are 3D printed; slight alignment variation is normal. A brief gentle heat application (≤ 100 °C) can encourage a true run.
4. If excessive vibration causes the coupler to loosen, a small drop of super-glue will fix it permanently — confirm it runs true before the glue sets.

---

### Trimming the Drive Shaft

Drive shafts are supplied longer than required. Trim to suit your cluster, typically flush with the motor housing. **Confirm the shaft does not bottom out in the cluster before tightening.**

---

### Fine-Tuning the Coupler

Once the motor is assembled, slide it over the OEM cluster shaft. Check:

- The drive pin engages cleanly
- The motor spins freely by hand
- The pin length is sufficient to engage the cluster but does not prevent the housing from seating fully

Take time here — good fitment minimises noise and extends coupler life.

> Couplers are 3D printed and every motor / housing / cluster pairing wears in slightly differently. Expect to revisit the on-board potentiometer once after a few hours of running so the top-end reading still hits full-scale cleanly.

---

## WiFi & Web Interface

Connect to the **`SpeedPulser`** WiFi access point (open network) and navigate to **`192.168.1.1`** or **`speedpulser.local`** in a browser.

The interface is a single-page app served from the ESP32's LittleFS flash partition, built on Forbes Automotive's shared dark theme (the same look used across OpenHaldex, Can2Cluster, SpeedPulserPro, can2rpm, the MQB Steering Wheel Controller and AirLift Controller). Settings are applied in real time and saved to EEPROM automatically every 2 seconds. Current firmware version: **4.01**.

**Status colours:** most tiles use plain text rather than colour, but a per-tile dial gauge (Incoming Speed / Motor Duty) turns **orange/amber** whenever **Speed Test Mode** or **Calibration Mode** is driving the motor — a reminder that the value shown is a synthetic test value, not a live vehicle reading. A missing motor-feedback signal (legacy PCBs without the feedback circuit) shows as plain **N/A** rather than a coloured warning.

### Dashboard Tab

Live read-outs updated automatically:

| Field | Description |
|---|---|
| Incoming Speed | Speed value calculated from incoming hall-sensor pulses (km/h or mph) |
| Motor Duty | PWM duty cycle currently applied to the motor |
| Measured Speed | Speed derived from the motor fedback pin when feedback is enabled |
| PID Trim | Duty correction the feedback loop is currently applying |
| Speed Offset Type | Whether a *Global* or *Curve* offset is active |
| Current Speed Offset | The offset value applied at the current speed |

Below the read-outs is a **Calibration Curve** graph: a duty-vs-speed trace of the
active calibration with its captured points, plus a marker showing the point
currently being achieved — from the hall input, Speed Test Mode or Calibration Mode.

While **Speed Test Mode** is active, these fields switch to show the chosen test speed and resulting motor duty instead.

<p align="center"><img src="/Images/ui-dashboard.png" alt="Dashboard tab — live data tiles (incoming speed, motor duty, offset type and value, measured speed, PID trim) and the calibration curve graph" width="300"></p>

> The bright dot on the graph is the point currently being achieved — from the hall input, Speed Test Mode or Calibration Mode — plotted against the active calibration's duty/speed curve (green dots = captured/sampled anchor points).

### Configuration Tab

| Setting | Description |
|---|---|
| Enable Needle Sweep | Triggers a full-scale needle sweep on power-up |
| Sweep Speed (ms) | Step delay in milliseconds: lower = faster sweep |
| Test Needle Sweep | Trigger a sweep immediately from the browser |
| Calibration Selection | Choose from 18 built-in cluster calibration profiles |
| Maximum Speed (km/h) | Upper end of the cluster's speed scale |
| Maximum Hall Frequency (Hz) | The input frequency that corresponds to Maximum Speed |
| Speed Offset Value | Fixed offset added to or subtracted from all speed readings |
| Positive Offset | Direction of the fixed offset (add or subtract) |
| Speed-Dependent Offset Curve | Enable a 5-point curve offset in place of the global offset |
| Cluster in MPH | Convert the km/h input signal to mph before looking up the motor duty |
| Average Filter Samples | Median smoothing window (1–10) for the incoming signal; higher = steadier but slower |

<p align="center">
  <img src="/Images/ui-configuration.png" alt="Configuration tab — calibration selection and needle sweep" width="300">
  &nbsp;&nbsp;
  <img src="/Images/ui-speed-limits.png" alt="Configuration tab — speed limits, the 5-point speed-dependent offset curve, cluster-in-MPH and the signal filter" width="300">
</p>

*Configuration: calibration profile and needle sweep (left); speed limits, the offset curve (or fixed offset), MPH conversion and the median filter window (right).*

### Advanced Tab

| Setting | Description |
|---|---|
| Reverse Direction | Drive the direction pin HIGH to flip the motor direction |
| Feedback Enable | Turn the closed-loop PID duty trim on or off (**not available on legacy PCBs**) |
| Min Feedback Speed (km/h) | Below this speed the loop runs open-loop (feed-forward only) to stop low-speed hunting; 0 = always closed-loop |
| PID Kp / Ki / Kd | Feedback loop gains (defaults 0.15 / 1.3 / 0) |
| PID Deadband (Hz) | Feedback error (in Hz) ignored by the loop, to stop the needle dithering |
| Reset PID Defaults | Restore the tuned default gains |
| Voltage Control *(V4 PCB only)* | Enable the closed-loop **buck-converter voltage** trim and set its nominal / minimum PWM, voltage range, trim gain and loop gains |
| Performance Array Value | Index of the active calibration array |
| Incoming Pulses | Raw frequency value from the ISR |
| Raw Count | Number of samples accumulated so far |
| LED Counter | ISR pulse counter (also drives the onboard LED blink) |

<p align="center">
  <img src="/Images/ui-advanced.png" alt="Advanced tab — motor direction, feedback enable, min feedback speed, PID gains and deadband" width="300">
  &nbsp;&nbsp;
  <img src="/Images/ui-status.png" alt="Advanced tab — live status monitor: performance array, incoming pulses, raw count, LED counter, measured speed / frequency, PID trim, buck state, voltage command and PWM fraction" width="300">
</p>
> **Reverse Direction** drives GPIO 10 HIGH instead of LOW, flipping the rotation direction for clusters whose motor is mounted the opposite way round.
> **Feedback Enable** turns the closed-loop PID trim on; the sliders below it tune the loop and **Reset PID Defaults** restores the tuned baseline (0.15 / 1.3 / 0). See [Closed-Loop Feedback (PID)](#closed-loop-feedback-pid) below for how it works.

On a **V4 PCB** the Advanced tab also shows **Voltage Control**. This is a mid-ranging scheme: the fast PID loop above trims PWM duty to hit the target speed, while a slow loop nudges the buck-converter voltage so the PWM stays near its **Nominal PWM** working point (between the **Minimum / Maximum Voltage** limits). Turn it off to fall back to fixed full-voltage PWM, i.e. legacy behaviour.

<p align="center">
  <img src="/Images/ui-voltage-control.png" alt="Advanced tab — Voltage Control card: enable, nominal and minimum PWM, minimum / maximum voltage, trim gain and PWM loop gains" width="300">
</p>

### Calibration Tab

Contains **Speed Test Mode**, **Export / Import** and — depending on the board — either the **Top Speed Calibration** card (V4 PCB, which can lift the motor supply voltage) or the multi-point **Calibration Builder** (earlier PCBs).

**Speed Test Mode**  
Locks the motor to a user-chosen speed so the cluster can be observed
without a speed signal. The chosen speed passes through the full offset and
calibration process, to give a realistic preview. With feedback enabled the closed
loop drives to that speed; the dashboard updates in real time to show the chosen speed
and the resulting motor duty.

**Top Speed Calibration** *(V4 PCB)*  
Tick **Enable Calibration Mode**, then raise **Voltage Lift** and/or **PWM Duty** until the
needle sits exactly on the top-speed mark (the Maximum Speed set on the Configuration tab)
and press **Capture Top Speed**. Every lower speed then scales linearly from that single
point; the low cut-off is **Min Feedback Speed** on the Advanced tab.

<p align="center">
  <img src="/Images/ui-speed-test.png" alt="Calibration tab — Speed Test Mode with enable and test speed slider" width="300">
  &nbsp;&nbsp;
  <img src="/Images/ui-top-speed.png" alt="Calibration tab — Top Speed Calibration (V4): calibration mode, voltage lift, PWM duty and Capture Top Speed" width="300">
</p>

**Calibration Builder** *(earlier PCBs)*  

1. Tick **Enable Calibration Mode** — the motor now follows the big duty read-out instead
   of the speed source.
2. Change the duty to the maximum value (**4096**).  Adjust the trimmer to achieve maximum cluster value.
3. Change the duty with the **−50 / −10 / −1 / +1 / +10 / +50** buttons until the needle sits exactly on a speed mark.
4. Pick that speed from the **Target speed** or type it in and press **Capture Point**. Each capture
   is listed under *Captured Points* and can be removed individually.
5. Repeat across the scale, name the calibration, then **Generate & Apply** to preview it
   live and **Save to Device** to store it.

**Export / Import**  
**Export Text** saves the active calibration as a `.txt` you can share or back up and
**Import Text** loads one back and applies it. **Export C-Array** writes a paste-ready
firmware preset (a `motorPerformanceN[]` table plus its `calibrationProfiles[]` line) for
building a calibration in permanently.

<p align="center">
  <img src="/Images/ui-calibration.png" alt="Calibration tab — Calibration Builder: calibration mode, duty jog buttons, target speed grid, capture point, name, Generate & Apply and Save to Device" width="300">
  &nbsp;&nbsp;
  <img src="/Images/ui-export-import.png" alt="Calibration tab — Export Text, Export C-Array and Import Text" width="300">
</p>
> Once at least two points are captured, **Generate & Apply** interpolates the full curve and **Save to Device** remembers it — it then appears as the **★ Custom** entry in the Configuration tab's calibration list.

### OTA Tab

Upload a new compiled `.bin` firmware file directly from the browser — no USB cable required. Two steps: the **Filesystem** image (`littlefs.bin`, the web UI) first, then the **Firmware** image (`firmware.bin`); the step tracker marks each as it completes and the device reboots automatically after the firmware step. See [Over-the-Air Updates](#over-the-air-updates-two-step).

<p align="center"><img src="/Images/ui-ota.png" alt="OTA tab — firmware info and the two-step filesystem / firmware uploader" width="300"></p>

---

## Closed-Loop Feedback (PID)

With **Feedback Enable** on (Advanced tab), the firmware measures the BLDC motor's feedback
pulses and trims the PWM duty so the needle holds its reading under load or voltage sag —
rather than relying on the open-loop calibration alone.

**How It Works**

- The feedback pulse on GPIO4 is counted and converted to a frequency (Hz), then
  smoothed with an exponential moving average.
- The requested speed is converted to a target frequency using a fixed full-scale
  reference (**254 Hz at maximum speed**, measured on the bench and defined into the
  firmware) so the loop needs no per-user frequency calibration.
- A PID controller calculates a duty correction that is added to the feed-forward
  calibration duty. A small dead-band with integral hold prevents needle shudder around
  the target.
- Below **Min Feedback Speed** the loop reverts to open-loop feed-forward to avoid
  low-speed hunting where the motor can't run smoothly.

**Tuning** — the default gains (Kp 0.15, Ki 1.3, Kd 0) suit the supplied motor and
cluster combination; the integral term carries most of the correction. Adjust from the
Advanced tab if needed, and use **Reset PID Defaults** to return to the tuned values.
The Status Monitor shows the live measured speed, the PID trim being applied and the raw
tacho frequency so you can see the loop working.

---

## Calibration — How It Works

### The Calibration Array

Each calibration profile is a **386-element `uint16_t` array** stored in flash (`PROGMEM`). The **array index** represents speed in km/h (0–385) and the **array value** is the 10-bit PWM duty cycle (0–1023) that drives the motor to produce the corresponding reading on that cluster.

```
index   0  →  duty   0   (motor off / dead band)
index  50  →  duty  ~60  (motor at ~50 km/h cluster reading)
index 120  →  duty ~130  (motor at ~120 km/h cluster reading)
  ...
index 200  →  duty ~195  (motor at full scale for a 200 km/h cluster)
```

Values near index 0 are `0` — the motor's dead band where it will not yet turn. Values plateau near the top because the cluster needle is at full deflection.

### Signal Processing 

```
Hall sensor pulse
    │
    ▼  incomingHz()  — windowed capture
    Accumulates each edge interval + count; rejects pulses < 3 ms (> 333 Hz) as noise
    │
    ▼  speedControlTask  (once per loop)
    frequency = intervals ÷ summed interval time   (true average over the window)
    map(frequency, 0, maxFreqHall, 0, maxSpeed)  →  speed in km/h
    │
    ▼  RunningMedian filter  (averageFilter samples, default 6)
    Smoothed median speed value
    │
    ▼  applyConfiguredSpeedOffset()
    Speed ± global offset  OR  speed ± curve offset for that band
    │
    ▼  Optional: × 0.621371  if "Cluster in MPH" is enabled
    │
    ▼  findClosestMatch()
    Scans motorPerformance[] for the closest matching speed value
    Returns the array index = 10-bit duty cycle
    │
    ▼  setMotorDuty()  — native LEDC IDF driver, 10 kHz
    Motor PWM output on GPIO 2
```

> **Windowed frequency capture:** Rather than deriving speed from a single edge-to-edge period — which turns tone-wheel / tooth-spacing jitter straight into a jumpy needle — the ISR accumulates the edge intervals and the task averages them once per window. Pulses closer than ~3 ms (> 333 Hz) are rejected, so ignition-coil EMI can't be mistaken for a valid speed.

> **Default hall-sensor scaling:** 1 Hz = 1 km/h. This matches 02J / 02M gearbox sensors used in most VW/Audi applications. Adjust `maxFreqHall` and `maxSpeed` together if your sensor has a different ratio (e.g. set both to 160 for a sensor that outputs 160 Hz at 160 km/h).

### Reading a Legacy Calibration Array

To check what duty a profile produces at a given speed, index directly:

```cpp
// Direct lookup — returns the 10-bit duty for 80 km/h on the active profile
uint16_t duty = motorPerformance[80];
```

`findClosestMatch()` does the inverse: given a target speed value, it walks the array to find the entry whose **value** is closest to the target and returns the **index** as the duty cycle. This gracefully handles calibration arrays that are not perfectly monotone.

---

## Speed Offset

Two offset modes are available and are both configured from the **Configuration** tab.

**Global Offset** (default)  
A single fixed value is added to or subtracted from every speed reading before the calibration lookup. Useful for correcting a systematic bias across the whole scale caused by motor preload or cluster wear.

**5-Point Curve**  
Five independent offsets replace the global offset when enabled. This corrects clusters whose response is non-linear and cannot be fixed by a single constant.

| Point | Speed band |
|---|---|
| 1 | 0 – 50 km/h |
| 2 | 50 – 100 km/h |
| 3 | 100 – 150 km/h |
| 4 | 150 – 200 km/h |
| 5 | 200+ km/h |

Each point accepts ±20 km/h. Enable the *Speed-Dependent Offset Curve* checkbox on the Configuration tab to activate.

---

## Power Management

The firmware includes a **universal reduced-power codeblock** (`power_manager` - used in SpeedPulser Pro, Can2Cluster and other projects) that activates automatically 1 minute after the last WiFi client disconnects. This cuts current through the on-board linear regulator, directly reducing its heat output — important for long ignition-on times.

**What changes when idle:**

| Action | Saving |
|---|---|
| WiFi radio off | ~80–120 mA average (single biggest saving) |
| CPU: 160 MHz → 80 MHz | Moderate reduction in active current |
| Bluetooth controller released at boot | ~60 KB RAM freed; small idle current saving |
| WiFi modem-sleep while clients are connected | Minor saving without losing connectivity |
| Reduced WiFi TX power | Adequate for in-car range; further small saving |

**Waking back up:**  
As soon as a device reconnects to the WiFi AP, full power is restored automatically — the radio comes back up, the CPU returns to 160 MHz, and the web server resumes. A power-cycle (ignition off/on) will also restore WiFi.

> The LOLIN C3 Mini's maximum CPU frequency is 160 MHz; the power manager auto-detects this at compile time and adjusts accordingly.

---

## Over-the-Air Updates (Two-Step)

New firmware can be flashed without removing the unit from the vehicle. Firmware and the web UI live on separate flash partitions, so updating is a **two-step process** using the shared `ota_manager` module:

1. **Filesystem** — in the **OTA** tab, select **Filesystem (web UI)** and upload `littlefs.bin` (`POST /api/ota/fs`, written to the SPIFFS partition). This updates `index.html` / `app.js` / `style.css`; the device does **not** reboot after this step, so you can go straight on to step 2.
2. **Firmware** — select **Firmware (application)** and upload `firmware.bin` (`POST /api/ota`, written to the OTA app partition). The device reboots automatically once this completes.

Steps in full:

1. Build the project in PlatformIO → locate `firmware.bin` in `.pio/build/lolin_c3_mini/` and `littlefs.bin` under the filesystem build output.
2. Connect to the `SpeedPulser` WiFi AP (or `speedpulser.local`).
3. Open the **OTA** tab in the browser.
4. Upload the filesystem image first, then the firmware image.
5. The device flashes and reboots automatically after the firmware step.

`GET /api/ota/info` reports the running version, board and hardware.

---

## Technical Reference

### Pin Assignments (LOLIN C3 Mini)

| GPIO | Function |
|---|---|
| 2 | Motor PWM output (LEDC 12-bit, stepped to 5 V via NPN transistor) |
| 4 | Motor tacho feedback input (falling-edge interrupt counter; used by the PID loop) |
| 5 | Speed pulse input (falling-edge interrupt) |
| 8 | Onboard LED (blinks to confirm incoming pulses) |
| 10 | Motor direction (LOW = normal, HIGH = reversed) |

### PWM Parameters

| Parameter | Value |
|---|---|
| Frequency | 10 kHz |
| Resolution | 12-bit (0–4095) |
| Driver | Native ESP-IDF `ledc_set_duty` / `ledc_update_duty` |

> Built-in calibration tables were captured at 10-bit and are scaled up to the 12-bit
> hardware domain automatically; the Calibration Builder captures new points at full
> 12-bit resolution.

### FreeRTOS Tasks

| Task | Core | Period |
|---|---|---|
| `speedControlTask` | 0 | Continuous loop |
| `eepromTask` | 0 | 2 000 ms |
| `powerManagerTask` | 0 | 5 000 ms check interval |

### PlatformIO Dependencies

| Library | Purpose |
|---|---|
| `mathieucarbou/ESPAsyncWebServer` | Async web server (ESP-IDF 5.x compatible fork) |
| `mathieucarbou/AsyncTCP` | Underlying TCP for AsyncWebServer |
| `bblanchon/ArduinoJson` | JSON serialisation for REST API |
| `RobTillaart/RunningMedian` | Median filter for speed smoothing |

Platform: `pioarduino/platform-espressif32` (Arduino-ESP32 3.x / ESP-IDF 5.x)

---

## Building & Flashing

1. Open the project folder in VS Code with the PlatformIO extension installed.
2. Confirm `env:lolin_c3_mini` is selected in `platformio.ini`.
3. **Build Filesystem Image** (PlatformIO sidebar) — packages the web UI files from `data/` into a LittleFS image.
4. **Upload Filesystem Image** — flashes the web UI to the SPIFFS/LittleFS partition.
5. **Build & Upload** — flashes the main firmware.
6. Open the Serial Monitor at 115 200 baud to confirm startup messages.

> Serial debug output is controlled by `-D serialDebug=1` in `platformio.ini`. Set to `0` for production builds to avoid stalling if no USB-CDC host is connected.

---

## Version History

| Version | Summary |
|---|---|
| V1.01 | Initial release |
| V1.05 | Global speed offset |
| V1.06 | Pulse timeout / reset-to-zero |
| V1.07 | 160 mph MK2 Golf calibration (Charlie) |
| V1.09 | Martin Springell MK1 Golf calibration |
| V1.11 | WiFi settings page |
| V1.12 | Mercedes W123 calibration |
| V1.13 | Calibration selection in WiFi |
| V1.15 | Calibration test mode via WiFi |
| V1.17 | Smiths 70 mph calibration |
| V1.18 | Over-the-Air updates |
| V1.19 | Smiths 90 mph calibration |
| V2.00 | Ported to PlatformIO |
| V2.10 | LEDC hardware PWM; FreeRTOS tasks; new REST API tabbed web UI; power management module |
| V2.20 | "Cluster in MPH" conversion option |
| V2.21 | Fixed LEDC driver for Arduino-ESP32 3.x (`ledc_set_duty` / `ledc_update_duty`) |
| V3.00 | PCB revision to include Motor Feedback + Reverse option and two Buck Converters (one for the motor, one for the ESP32) |
| V3.01 | PID Calibration, Calibration Builder + Calibration Curve |
| V3.02 | Smoother needle sweep | 
| V3.0x | Various multiple tweaks |
| V3.10 | Tighter PID control, feedback check for legacy PCBs etc |