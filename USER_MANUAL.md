# Landy Gauge — User Manual

## Overview

The Landy Gauge is a compact round instrument display designed for Land Rover vehicles. It features a 360×360 pixel circular screen with touch input, physical navigation buttons, and an expansion board for vehicle signal inputs and outputs.

The gauge provides seven display screens that cycle in order:

1. **Clock**
2. **Boost** (turbo boost pressure)
3. **EGT** (exhaust gas temperature)
4. **Cooling** (fan status, coolant level, coolant temperature)
5. **Tire Pressure** (BLE TPMS)
6. **Tilt** (vehicle roll angle / inclinometer)
7. **Compass** (magnetic heading)

Gauges that require the expansion board (Boost, EGT, Compass, Cooling) are automatically skipped if the expansion board is not detected.

---

## Display Modes

The gauge supports two display modes:

- **Day Mode** — White/bright accents on a dark background. Used when vehicle lights are off.
- **Night Mode** — Green accents on a dark background with reduced backlight brightness. Activates automatically when the vehicle head lights are switched on (detected via the expansion board lights input).

---

## Navigation

### Touch Screen

The circular screen is divided into touch zones:

- **Tap left half** — Switch to the previous gauge
- **Tap right half** — Switch to the next gauge
- **Tap centre** (within the central circle) — Gauge-specific action (see individual gauge sections below)
- **Double-tap centre** — Jump directly to the Clock gauge from any screen
- **Long press centre** — Gauge-specific long-press action (see individual gauge sections below)

> **Note:** On the Cooling gauge, the long-press zones are different — see the Cooling section for details.

### Physical Buttons

Three physical buttons are available on the Waveshare board:

- **Next button (GPIO43)** — Switch to the next gauge
- **Previous button (GPIO44)** — Switch to the previous gauge
- **Boot button (GPIO0)** — Also functions as a next gauge button

### Expansion Board Select Button

The expansion board provides an additional select button:

- **Single tap** — Gauge-specific action (same as touch centre tap)
- **Double tap** — Jump to the Clock gauge
- **Long press (hold for 1 second)** — Gauge-specific long-press action. On the Cooling gauge this toggles wading mode.

A short beep confirms when a long press is accepted.

---

## Power Management

### Ignition Detection

The gauge monitors the vehicle ignition state through the expansion board:

- **Ignition ON** — Normal operation. All gauges and sensors are active.
- **Ignition OFF** — The gauge enters **standby mode**: the display turns off, WiFi and Bluetooth scanning stop, and the CPU reduces to 80 MHz to conserve power.

### Temporary Wake

When the ignition is off, touching the screen or pressing the expansion board select button will temporarily wake the display for **5 minutes**. After 5 minutes of ignition-off operation, the gauge returns to standby.

If the ignition comes on during a temporary wake, the gauge stays active permanently.

### Inactivity Timeout

After **5 minutes** of inactivity (no touch or button input) with the ignition on, the gauge automatically returns to the Clock screen.

---

## Gauge Screens

### 1. Clock

Displays an analogue clock face with hour and minute. Time is maintained by an onboard RTC (real-time clock).

**Centre tap action:** None (double-tap from any gauge jumps to the Clock screen).

**Long press action:** Force an NTP time sync via your **iPhone hotspot**. The gauge connects to your phone's personal hotspot, synchronises the time via the internet, updates the RTC, then disconnects WiFi. This bypasses the 24-hour cooldown, so you can force a sync at any time regardless of when the last sync occurred.

> **Note:** An automatic NTP sync also runs once per day using the **home WiFi network** when it is in range. A 24-hour cooldown prevents unnecessary syncs on short drives. The automatic sync uses the home network; only the manual long-press sync uses the iPhone hotspot.

<!-- IMAGE: Clock gauge screenshot (day mode) -->
<!-- IMAGE: Clock gauge screenshot (night mode) -->

---

### 2. Boost (Turbo Boost Pressure)

Displays turbo boost pressure on an analogue dial gauge. The sensor is a 2-bar MAP (Manifold Absolute Pressure) sensor connected to the ADS1115 ADC on the expansion board.

- **Range:** 0–2.0 Bar (also displayable in PSI: 0–29 PSI)
- **Warning zone:** Above 1.5 Bar — the needle and tick marks turn red/orange
- **Vacuum is not shown** — negative pressure reads as zero

**Centre tap action:** Toggle display units between **Bar** and **PSI**. The preference is saved and persists across reboots.

<!-- IMAGE: Boost gauge screenshot -->

---

### 3. EGT (Exhaust Gas Temperature)

Displays exhaust gas temperature from an MCP9600 thermocouple converter on the expansion board.

- **Range:** 0–900°C (or 0–1650°F)
- **Warning (yellow):** Above 680°C / 1256°F
- **Danger (red):** Above 750°C / 1382°F

When the EGT reaches the danger threshold, the gauge will automatically switch to the EGT screen if you are viewing another gauge, accompanied by an audio alert.

**Centre tap action:** Toggle display units between **°C** and **°F**. The preference is saved and persists across reboots.

<!-- IMAGE: EGT gauge screenshot -->

---

### 4. Cooling

Displays the cooling system status in three sectors:

- **Top-left — Fan Low:** Shows a rotating fan icon when the low-speed cooling fan is active (detected via expansion board input IN3).
- **Top-right — Fan High:** Shows a rotating fan icon when the high-speed cooling fan is active (detected via expansion board input IN4).
- **Bottom — Coolant Level & Temperature:** Shows a radiator icon with a water level indicator. Coolant level is monitored via a float switch (expansion board input IN5). Coolant temperature is read from an NTC sender via the ADS1115 ADC.

**Coolant Temperature Display:**

- Hidden below 60°C
- Normal colour above 60°C
- **Yellow warning** at 110°C
- **Red danger** at 115°C — plays an overheat audio alert

The coolant level sensor uses an intelligent filter that considers vehicle tilt and G-forces (from the onboard IMU) to avoid false alarms caused by fluid sloshing during braking, acceleration, or cornering. A low-coolant alarm is only confirmed after 3 seconds of consistent low readings while the vehicle is settled.

**Automatic alarm switch:** The gauge automatically switches to the Cooling screen if fan low activates, coolant level drops, or coolant overtemperature is detected.

#### Cooling Gauge Touch Zones

The Cooling gauge has three long-press zones (different from other gauges):

| Screen Area | Long Press Action |
|---|---|
| **Top-left** (over the Fan Low icon) | Toggle manual fan low override ON/OFF |
| **Top-right** (over the Fan High icon) | Toggle manual fan high override ON/OFF |
| **Bottom half** | Toggle wading mode ON/OFF |

A confirmation beep sounds when each long press is accepted.

#### Manual Fan Override

Press and hold over the fan low or fan high icon to manually activate or deactivate that fan relay:

- **Fan Low** — Controls expansion board output GPA1
- **Fan High** — Controls expansion board output GPA2

Audio confirmation plays when toggling (fanLowOn.mp3 / fanLowOff.mp3, fanHighOn.mp3 / fanHighOff.mp3).

> **Important:** Manual fan override is **blocked while wading mode is active**. Fans cannot be manually toggled until wading mode is turned off.

#### Wading Mode

Long-press the bottom half of the Cooling screen (or long-press the expansion board select button while on the Cooling gauge) to toggle wading mode:

- **WADING ON:** Activates the wading output relay (expansion board output GPA0 / OUT1). Both fan icons turn **red**. Fan rotation animation stops. A "WADING" label appears on screen. Plays wadeOn.mp3.
- **WADING OFF:** Deactivates the relay. Fan icons return to normal colours. Plays wadeOff.mp3.

When wading mode is activated:
- Any active manual fan overrides are automatically turned off
- You **cannot navigate away** from the Cooling gauge while wading mode is active — this prevents accidentally forgetting that wading mode is engaged
- Manual fan toggling is disabled

> **Safety:** Wading mode persists even if the gauge is reinitialised. It must be explicitly turned off by another long press.

<!-- IMAGE: Cooling gauge screenshot (normal) -->
<!-- IMAGE: Cooling gauge screenshot (wading mode active) -->
<!-- IMAGE: Cooling gauge touch zone diagram -->

---

### 5. Tire Pressure (BLE TPMS)

Displays tire pressure, temperature, and sensor battery level for all four tires. Data is received wirelessly via Bluetooth Low Energy (BLE) from TPMS sensors mounted on each wheel.

The display shows a roof-view outline of the vehicle with pressure readings at each corner:

- **Front Left** — top-left
- **Front Right** — top-right
- **Rear Left** — bottom-left
- **Rear Right** — bottom-right

Each wheel position shows pressure, temperature, and battery percentage when data is available.

**Centre tap action:** Cycle through display unit combinations:
- BAR / °C
- PSI / °C
- BAR / °F
- PSI / °F

The preference is saved and persists across reboots.

**Automatic alarm switch:** The gauge automatically switches to the Tire Pressure screen if:
- Any tire drops below **15 PSI** (1.03 Bar)
- A rapid pressure drop of **5 PSI or more within 60 seconds** is detected on any tire

> **Note:** BLE scanning runs continuously in the background. When viewing the Tire Pressure gauge, a faster scan rate is used for quicker updates.

<!-- IMAGE: Tire pressure gauge screenshot -->

---

### 6. Tilt (Inclinometer)

Displays the vehicle's roll angle using the onboard IMU (Inertial Measurement Unit). A rear-view image of the vehicle tilts to show the current inclination.

- **Normal (white/green):** 0–29°
- **Yellow warning zone:** 30°+ — triggers a warning audio alert
- **Red danger zone:** 35°+ — triggers a danger audio alert with more frequent beeps

**Centre tap action:** Zero the tilt gauge at the current angle. This sets the current physical orientation as the 0° reference point. Useful for calibrating on uneven ground. The offset is saved and persists across reboots.

**Automatic alarm switch:** The gauge automatically switches to the Tilt screen when roll exceeds 30°.

> **Note:** After the alarm condition clears, the gauge automatically returns to your previous screen after 30 seconds.

<!-- IMAGE: Tilt gauge screenshot (level) -->
<!-- IMAGE: Tilt gauge screenshot (tilted with warning) -->

---

### 7. Compass

Displays a compass rose with the current magnetic heading from a LIS3MDL magnetometer on the expansion board. The compass card rotates to show cardinal (N, E, S, W) and intercardinal directions with a fixed heading indicator at top.

**Centre tap action:** Toggle compass calibration mode:

1. **First tap** — Enters calibration mode. A "CALIBRATING" overlay appears. Slowly rotate the vehicle (or the gauge unit) through a full 360° in all orientations to allow the magnetometer to sample the full magnetic field.
2. **Second tap** — Exits calibration mode. The hard-iron offsets and soft-iron scale factors are calculated, applied, and saved to permanent storage.

The calibration data persists across reboots.

<!-- IMAGE: Compass gauge screenshot -->

---

## Automatic Alarm Switching

The gauge monitors several alarm conditions in the background and will automatically switch to the relevant screen when triggered. Alarms are checked in priority order:

| Priority | Alarm Condition | Target Screen |
|---|---|---|
| 1 (Highest) | Tilt ≥ 30° | Tilt |
| 2 | EGT over-temperature | EGT |
| 3 | Cooling alarm (fan active, coolant low, or overtemp) | Cooling |
| 4 | TPMS rapid pressure drop (≥5 PSI in 60s) | Tire Pressure |
| 5 (Lowest) | TPMS low pressure (any tire < 15 PSI) | Tire Pressure |

**After an alarm switch:**
- A 5-second lockout prevents the alarm from fighting with manual navigation
- If the alarm condition clears, the gauge returns to your previous screen after **30 seconds**
- Manually navigating away from the alarm screen cancels the auto-return

---

## Audio Alerts

The gauge plays audio alerts through the onboard speaker for various events. Audio files are stored on the SD card:

| Event | Audio File |
|---|---|
| Tilt warning (30°+) | WarningRoll.mp3 |
| Tilt danger (35°+) | DangerRoll.mp3 |
| Coolant overtemperature | overheat.mp3 |
| Wading mode ON | wadeOn.mp3 |
| Wading mode OFF | wadeOff.mp3 |
| Fan Low manual ON | fanLowOn.mp3 |
| Fan Low manual OFF | fanLowOff.mp3 |
| Fan High manual ON | fanHighOn.mp3 |
| Fan High manual OFF | fanHighOff.mp3 |
| Button/action confirmation | Short beep (internal) |

---

## Settings Persistence

The following settings are saved to non-volatile storage (NVS) and persist across power cycles:

- Boost gauge units (Bar / PSI)
- Tire pressure display mode (Bar/PSI, °C/°F)
- EGT units (°C / °F)
- Tilt zero-offset angle
- Compass calibration data (hard-iron offsets and soft-iron scales)
- NTP last-sync timestamp (24-hour cooldown)

Settings are saved automatically when changed and restored at boot.

---

## Expansion Board Connections

### Digital Inputs (active high)

| Input | Signal | Description |
|---|---|---|
| IO0 | Select | Expansion board select button |
| IO1 | Ignition | Ignition on signal |
| IO2 | Lights | Headlights on signal |
| IO3 | Fan Low | Low-speed cooling fan active |
| IO4 | Fan High | High-speed cooling fan active |
| IO5 | Coolant Low | Coolant level float switch |
| IO6 | Spare | Unused |
| IO7 | Spare | Unused |

### Digital Outputs

| Output | Pin | Function |
|---|---|---|
| OUT1 / GPA0 | MCP23017 GPA0 | Wading mode relay |
| Fan Low | MCP23017 GPA1 | Manual fan low speed relay |
| Fan High | MCP23017 GPA2 | Manual fan high speed relay |

### Analogue Inputs (ADS1115 ADC)

| Channel | Signal | Description |
|---|---|---|
| AIN0 | MAP Sensor | 2-bar boost pressure sensor (0–5V via resistor divider) |
| AIN1 | Coolant Temp | NTC temperature sender (Land Rover type, 50–120°C range) |

### Magnetometer

- LIS3MDL 3-axis magnetometer for compass heading

### Thermocouple

- MCP9600 thermocouple-to-digital converter for EGT

---

## Quick Reference

| Action | How To |
|---|---|
| Next gauge | Tap right side of screen, or press Next button |
| Previous gauge | Tap left side of screen, or press Prev button |
| Jump to Clock | Double-tap centre of screen, or double-tap Select button |
| Gauge action | Tap centre of screen, or tap Select button |
| Long-press action | Long-press centre of screen, or hold Select button for 1 second |
| Toggle fan low (Cooling gauge) | Long-press top-left of screen (over fan low icon) |
| Toggle fan high (Cooling gauge) | Long-press top-right of screen (over fan high icon) |
| Toggle wading (Cooling gauge) | Long-press bottom half of screen, or hold Select button |
| Force NTP sync (Clock gauge) | Long-press centre of clock (uses iPhone hotspot) |
| Toggle boost units | Tap centre of boost gauge |
| Toggle EGT units | Tap centre of EGT gauge |
| Toggle TPMS units | Tap centre of tire pressure gauge |
| Zero tilt gauge | Tap centre of tilt gauge |
| Calibrate compass | Tap centre of compass gauge (tap again to finish) |
| Wake from standby | Touch screen or press Select button (5-minute temporary wake) |
