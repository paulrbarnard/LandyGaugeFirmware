# Landy Gauge — Comprehensive Test Plan

## Test Jig Setup

The gauge is mounted in a test jig with the ability to simulate all expansion board inputs:

| Signal | MCP23017 Pin | Test Jig Control | Active Level |
|--------|-------------|------------------|--------------|
| Select button | GPB0 (IO0) | Reserved (future expansion) | — |
| Ignition | GPB1 (IO1) | Toggle switch | HIGH |
| Lights (sidelights) | GPB2 (IO2) | Toggle switch | HIGH |
| Fan Low | GPB3 (IO3) | Toggle switch | HIGH |
| Fan High | GPB4 (IO4) | Toggle switch | HIGH |
| Coolant Low (float) | GPB5 (IO5) | Toggle switch | HIGH |
| Low Beam (dip) | GPB6 (IO6) | Toggle switch | HIGH |
| Full Beam (high) | GPB7 (IO7) | Toggle switch | HIGH |

**Analogue inputs** (ADS1115):
- AIN0 — MAP sensor: use a potentiometer (0–2.06V) or voltage source to simulate boost pressure
- AIN1 — Coolant temp: use a potentiometer (0.68–1.88V) to simulate NTC sender (0.68V ≈ 120°C, 1.88V ≈ 50°C)

**Outputs to verify** (MCP23017 Port A — check with LEDs or multimeter):
- GPA2 — Wading relay
- GPA3 — Fan Low override relay
- GPA4 — Fan High override relay

**Other equipment needed:**
- SD card with all MP3 files (8.3 format names)
- TPMS sensors on wheels (or within BLE range)
- Speaker/headphones connected to audio DAC
- Serial monitor for log verification

---

## Test Categories

- **[P]** = Pass / **[F]** = Fail / **[S]** = Skip / **[N]** = Notes

---

## 1. BOOT & INITIALISATION

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 1.1 | Cold boot with expansion board | Power on with ignition HIGH | Clock gauge displays, day mode, all I2C devices detected in logs | [ ] |
| 1.2 | Cold boot without expansion board | Disconnect expansion board, power on | Clock gauge displays, log shows "Expansion board not detected", expansion-dependent gauges skipped | [ ] |
| 1.3 | SD card present | Boot with SD card inserted | Log shows "SD card mounted", MP3 playback available | [ ] |
| 1.4 | SD card absent | Boot without SD card | System boots normally, audio alerts silently fail without crash | [ ] |
| 1.8 | SD card custom images loaded | Boot with .bin image files on SD card | Log shows "Custom images: N of 8 loaded from SD card" | [ ] |
| 1.9 | SD card no custom images | Boot with SD card but no .bin files | Log shows "Custom images: 0 of 8 loaded from SD card", built-in images used | [ ] |
| 1.5 | Touch screen detection | Boot with touch connected | Log shows "Touch screen detected and enabled" | [ ] |
| 1.6 | Button-only mode | Boot without touch (if possible) | Log shows "No touch screen detected — button-only mode", buttons still navigate | [ ] |
| 1.7 | NVS settings restore | Change a setting (e.g. boost units), reboot | Setting persists after reboot | [ ] |

---

## 2. NAVIGATION — TOUCH SCREEN

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 2.1 | Tap right half → next gauge | On Clock, tap right side | Switches to Boost gauge | [ ] |
| 2.2 | Tap left half → previous gauge | On Boost, tap left side | Returns to Clock gauge | [ ] |
| 2.3 | Full cycle forward | Tap right repeatedly through all gauges | Clock → Boost → EGT → Cooling → TPMS → Tilt → Compass → Clock | [ ] |
| 2.4 | Full cycle backward | Tap left repeatedly through all gauges | Clock → Compass → Tilt → TPMS → Cooling → EGT → Boost → Clock | [ ] |
| 2.5 | Double-tap centre → Clock | On any non-Clock gauge, double-tap centre | Jumps to Clock | [ ] |
| 2.6 | Double-tap on Clock | Already on Clock, double-tap centre | Nothing happens (no crash, no switch) | [ ] |
| 2.7 | Long-press centre (Boost) | On Boost gauge, long-press centre (~1s) | Units toggle BAR ↔ PSI, display updates | [ ] |
| 2.8 | Touch debounce | Rapidly tap right side multiple times | Only one gauge switch per 300ms, no double-skip | [ ] |
| 2.9 | Centre single-tap ignored | On any gauge, single-tap centre, wait >500ms | No action triggered (single tap is deferred for double-tap detection) | [ ] |

---

## 3. NAVIGATION — PHYSICAL BUTTONS

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 3.1 | Next button (GPIO43) | Press Next button | Switches to next gauge | [ ] |
| 3.2 | Previous button (GPIO44) | Press Previous button | Switches to previous gauge | [ ] |
| 3.3 | Boot button (GPIO0) | Press Boot button | Functions as Next gauge button | [ ] |
| 3.4 | Button combo → Select | Hold Next + Previous together | Emulates Select button (triggers double-tap/long-press logic) | [ ] |
| 3.5 | Button combo tolerance | Press Next, then Previous within 150ms | Registers as combo (select), not two separate presses | [ ] |
| 3.6 | Combo double-tap → Clock | On any gauge, do combo twice quickly | Jumps to Clock | [ ] |
| 3.7 | Combo long-press → action | On Boost gauge, hold both buttons >1s | Toggles boost units | [ ] |
| 3.8 | Button debounce | Rapidly press Next button | Only one switch per debounce period (50ms) | [ ] |

---

## 4. NAVIGATION — EXPANSION BOARD SELECT BUTTON

> **Section removed** — The dedicated expansion board select button (IO0) has been removed. IO0 is now reserved for future expansion. Select functionality is available via the Next+Previous button combo and touch screen double-tap/long-press.

---

## 5. POWER MANAGEMENT — IGNITION

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 5.1 | Ignition OFF → standby | Set ignition switch LOW | Display turns off, backlight off, log shows standby entry, CPU → 80 MHz | [ ] |
| 5.2 | Ignition ON → active | Set ignition switch HIGH | Display turns on, Clock gauge shown, CPU → 160 MHz, log shows wake | [ ] |
| 5.3 | Ignition cycle | Toggle ignition OFF then ON again | Standby → active cycle works cleanly, Clock shown on wake | [ ] |
| 5.4 | Standby sensor stop | In standby, check BLE and WiFi | BLE scanning stops, WiFi disconnected | [ ] |

---

## 6. POWER MANAGEMENT — TEMPORARY WAKE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 6.1 | Touch wake | Ignition OFF (standby), touch screen | Display wakes for 5 minutes, shows gauges | [ ] |
| 6.2 | Combo button wake | Ignition OFF, hold Next + Previous | Display wakes for 5 minutes | [ ] |
| 6.3 | Temp wake timeout | Wake via touch, wait 5 minutes with no interaction | Display returns to standby | [ ] |
| 6.4 | Temp wake activity reset | Wake via touch, interact at 4 minutes | 5-minute timer resets, stays awake another 5 minutes | [ ] |
| 6.5 | Ignition ON during temp wake | Wake via touch, then set ignition HIGH | Gauge stays active permanently (no 5-min timeout) | [ ] |

---

## 7. DISPLAY MODES — DAY & NIGHT

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 7.1 | Day mode default | Boot with all lights OFF | White/bright accents, backlight at ~70% | [ ] |
| 7.2 | Night mode activation | Set sidelights (IO2) + dip (IO6) HIGH | Green accents, backlight dims to ~15% | [ ] |
| 7.3 | Night mode on all gauges | Cycle through all 8 gauges with night mode active | Each gauge renders in night mode colours | [ ] |
| 7.4 | Day/night transition | Toggle sidelights + dip ON/OFF while viewing any gauge | Colours and brightness switch seamlessly | [ ] |
| 7.5 | Night mode persistence | Activate night mode, switch gauges | Night mode preserved when switching between gauges | [ ] |
| 7.6 | Sidelights only — no night mode | Set sidelights (IO2) HIGH, dip and full beam OFF | Stays in day mode (sidelights alone not sufficient) | [ ] |
| 7.7 | Full beam flash — no night mode | Flash full beam (IO7) HIGH briefly, sidelights OFF | Stays in day mode (headlights without sidelights ignored) | [ ] |
| 7.8 | Full beam + sidelights — night mode | Set sidelights (IO2) + full beam (IO7) HIGH | Night mode activates (full beam counts as headlights) | [ ] |
| 7.9 | Solar night — no expansion board | Disconnect expansion board, set RTC to 22:00, London timezone | Night mode activates automatically (civil twilight calc) | [ ] |
| 7.10 | Solar day — no expansion board | Disconnect expansion board, set RTC to 12:00, London timezone | Day mode active | [ ] |
| 7.11 | Solar dusk transition | Disconnect expansion board, set RTC a few minutes before expected dusk | Mode transitions from day to night at civil dusk | [ ] |
| 7.12 | Solar dawn transition | Disconnect expansion board, set RTC a few minutes before expected dawn | Mode transitions from night to day at civil dawn | [ ] |
| 7.13 | Solar — summer vs winter | Set date to June 21, then December 21, check dusk time | Summer dusk is later than winter dusk | [ ] |
| 7.14 | Solar — equatorial timezone | Select Thailand (+7, lat 13.8°), set RTC to 19:00 | Night mode activates (near-equator: dusk ~18:30 year-round) | [ ] |

---

## 8. INACTIVITY TIMEOUT

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 8.1 | Timeout to Clock | With ignition ON, navigate to Boost, wait 10 minutes with no input | Auto-switches to Clock, log shows "Inactivity timeout" | [ ] |
| 8.2 | Activity resets timer | Navigate to Boost, wait 9 minutes, tap screen, wait another 10 minutes | Timeout triggers 10 min after the last tap, not from initial switch | [ ] |
| 8.3 | No timeout on Clock | Already on Clock, wait >10 minutes | No switch occurs (already on Clock) | [ ] |
| 8.4 | No timeout during alarm | Alarm auto-switched to Tilt (roll >30°), wait 10 min | Inactivity timeout does NOT fire while alarm is active | [ ] |
| 8.5 | No timeout ignition OFF | Ignition OFF, temp wake active, wait 10 min | Temp wake timeout fires (5 min), NOT inactivity timeout | [ ] |

---

## 9. COOLING GAUGE

### 9a. Fan Display

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.1 | Fan low icon — active | Set fan low HIGH (IO3) | Left fan icon appears with rotating animation | [ ] |
| 9.2 | Fan low icon — inactive | Set fan low LOW (IO3) | Left fan icon stops rotating | [ ] |
| 9.3 | Fan high icon — active | Set fan high HIGH (IO4) | Right fan icon appears with rotating animation | [ ] |
| 9.4 | Fan high icon — inactive | Set fan high LOW (IO4) | Right fan icon stops rotating | [ ] |
| 9.5 | Both fans active | Set both IO3 and IO4 HIGH | Both fan icons rotate simultaneously | [ ] |

### 9b. Coolant Level

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.6 | Coolant level OK | Coolant low input LOW (IO5) | Radiator icon shows normal water level | [ ] |
| 9.7 | Coolant low — steady | Set coolant low HIGH (IO5) with gauge stationary for >3 seconds | After 3-second confirmation, low coolant alarm triggers, plays coollow.mp3 | [ ] |
| 9.8 | Coolant low — bounce rejection | Briefly pulse IO5 HIGH for <3 seconds, then LOW | No alarm — filter rejects transient | [ ] |
| 9.9 | Coolant low MP3 one-shot | Trigger coolant low, wait for MP3, then clear and re-trigger | MP3 plays only once per confirmed low event (resets on clear) | [ ] |

### 9c. Coolant Temperature

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.10 | Temp hidden <60°C | Set AIN1 to ~1.88V (≈50°C) | No temperature reading displayed | [ ] |
| 9.11 | Temp shown ≥60°C | Adjust AIN1 to show ~70°C | Temperature reading appears in normal colour | [ ] |
| 9.12 | Temp yellow warning 110°C | Adjust AIN1 to ~0.80V (≈110°C) | Temperature turns yellow | [ ] |
| 9.13 | Temp red danger 115°C | Adjust AIN1 to ~0.72V (≈115°C) | Temperature turns red, plays overheat.mp3 | [ ] |

### 9d. Wading Mode

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.14 | Wading ON — touch | On Cooling gauge, long-press bottom half of screen | GPA2 output goes HIGH, fan icons turn red, "WADING" label shows, wadeOn.mp3 plays | [ ] |
| 9.15 | Wading ON — button combo | On Cooling gauge, hold Next+Prev >1s | Same as 9.14 — wading activates | [ ] |
| 9.16 | Wading — navigation blocked | With wading ON, try to tap left/right to change gauge | Gauge does NOT change — locked on Cooling | [ ] |
| 9.17 | Wading OFF | Long-press bottom half again (or hold select) | GPA2 goes LOW, fan icons return to normal colours, wadeOff.mp3 plays | [ ] |
| 9.18 | Wading kills fan overrides | Activate fan low override, then activate wading | Fan low override turns OFF when wading activates | [ ] |
| 9.19 | Fan override blocked in wading | With wading ON, long-press fan low icon | No action — fan override rejected while wading active | [ ] |

### 9e. Manual Fan Overrides

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.20 | Fan low override ON | On Cooling gauge, long-press top-left (fan low icon) | GPA3 goes HIGH, fan low icon animates, fanLowOn.mp3 plays | [ ] |
| 9.21 | Fan low override OFF | Long-press top-left again | GPA3 goes LOW, fan low icon stops (if hardware signal also LOW), flooff.mp3 plays | [ ] |
| 9.22 | Fan high override ON | Long-press top-right (fan high icon) | GPA4 goes HIGH, fan high icon animates, fhion.mp3 plays | [ ] |
| 9.23 | Fan high override OFF | Long-press top-right again | GPA4 goes LOW, fhioff.mp3 plays | [ ] |
| 9.24 | Fan override 5-min timeout | Activate fan low override, wait 5 minutes | Override auto-deactivates, GPA3 goes LOW, off MP3 plays, log shows timeout | [ ] |
| 9.25 | Fan override + hardware signal | Activate fan low override (GPA3), also set IO3 HIGH | Fan icon animates from both sources; turning off override still shows animation from hardware signal | [ ] |
| 9.26 | Fan override timeout — background | Activate fan high override, switch to another gauge, wait 5 min | Override still times out in background, GPA4 goes LOW, off MP3 plays | [ ] |

### 9f. Cooling Auto-Switch Alarm

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 9.27 | Fan low triggers auto-switch | Viewing Boost gauge, set IO3 (fan low) HIGH | Auto-switches to Cooling gauge | [ ] |
| 9.28 | Coolant low triggers auto-switch | Viewing any gauge, trigger confirmed coolant low | Auto-switches to Cooling gauge | [ ] |
| 9.29 | Overtemp triggers auto-switch | Viewing any gauge, set AIN1 voltage to ≥115°C | Auto-switches to Cooling gauge | [ ] |
| 9.30 | Alarm return after clear | After auto-switch to Cooling, set all cooling inputs to normal | Returns to previous gauge after 30 seconds | [ ] |
| 9.31 | Manual nav cancels return | Auto-switch to Cooling, then manually tap right to next gauge | No auto-return to original gauge | [ ] |

---

## 10. BOOST GAUGE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 10.1 | Zero boost at atmosphere | AIN0 at atmospheric voltage (~1.0V) | Gauge reads 0 bar / 0 PSI | [ ] |
| 10.2 | Mid-range boost | Adjust AIN0 to simulate ~1.0 bar boost | Needle at ~50% of scale, reading ~1.0 bar (or ~14.5 PSI) | [ ] |
| 10.3 | Full-scale boost | Adjust AIN0 to maximum (~2.06V) | Needle near full scale ~2.0 bar | [ ] |
| 10.4 | Warning zone colour | Increase AIN0 past 1.5 bar | Needle and tick marks turn red/orange | [ ] |
| 10.5 | Negative pressure → zero | Set AIN0 below atmospheric | Gauge reads 0, no negative values shown | [ ] |
| 10.6 | Unit toggle BAR ↔ PSI | Long-press centre, toggle units | Display switches between BAR and PSI, values recalculate | [ ] |
| 10.7 | Unit persistence | Toggle to PSI, reboot | Still shows PSI after reboot | [ ] |

---

## 11. EGT GAUGE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 11.1 | Room temperature read | MCP9600 at ambient temp | Shows ~20–25°C (or equivalent °F) | [ ] |
| 11.2 | No MCP9600 present | Disconnect MCP9600 / no expansion board | EGT gauge skipped in cycle, or shows 0 gracefully | [ ] |
| 11.3 | Warning at 680°C | Simulate or wait for 680°C reading | Display turns yellow, egtwar.mp3 plays once | [ ] |
| 11.4 | Danger at 750°C | Simulate 750°C reading | Display turns red, egtdang.mp3 plays once | [ ] |
| 11.5 | EGT MP3 one-shot | Cross 680°C threshold multiple times | MP3 plays only on first crossing (resets when dropping below) | [ ] |
| 11.6 | EGT auto-switch alarm | Viewing another gauge, EGT crosses 680°C | Auto-switches to EGT gauge | [ ] |
| 11.7 | Unit toggle °C ↔ °F | Long-press centre on EGT gauge | Display toggles between °C and °F | [ ] |
| 11.8 | Unit persistence | Toggle to °F, reboot | Still shows °F after reboot | [ ] |

> **Note:** EGT thresholds are difficult to simulate with a test jig unless you can heat the thermocouple. You may need to temporarily lower the threshold constants for testing, or verify via serial log with a heat source.

---

## 12. TIRE PRESSURE (BLE TPMS)

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 12.1 | Sensor detection | TPMS sensors within BLE range | All 4 tire positions show pressure, temperature, battery | [ ] |
| 12.2 | Sensor data update | Observe TPMS screen for ~30s | Values update as BLE scans complete | [ ] |
| 12.3 | Fast scan on TPMS screen | Switch to TPMS gauge, watch log | Scan interval drops to ~3 seconds (fast mode) | [ ] |
| 12.4 | Normal scan off TPMS screen | Switch away from TPMS gauge, watch log | Scan interval returns to ~30 seconds | [ ] |
| 12.5 | Battery colour — red | Sensor with battery <2% | Battery percentage text is red | [ ] |
| 12.6 | Battery colour — yellow | Sensor with battery 2–5% | Battery percentage text is yellow | [ ] |
| 12.7 | Battery colour — normal | Sensor with battery >5% | Battery percentage text is white/green (accent) | [ ] |
| 12.8 | Unit cycle | Long-press centre on TPMS gauge 4 times | Cycles: BAR/°C → PSI/°C → BAR/°F → PSI/°F → BAR/°C | [ ] |
| 12.9 | Unit persistence | Set to PSI/°F, reboot | Still shows PSI/°F after reboot | [ ] |
| 12.10 | Low pressure auto-switch | Deflate a tire below 15 PSI (or use a sensor reporting <15 PSI) | Auto-switches to TPMS gauge | [ ] |
| 12.11 | Pressure drop alarm | Rapidly decrease a tire's pressure by ≥5 PSI within 60 seconds | Auto-switches to TPMS gauge, tirewar.mp3 plays | [ ] |
| 12.12 | TPMS alarm MP3 one-shot | Trigger pressure drop alarm twice | tirewar.mp3 plays only on first switch (resets after alarm clears) | [ ] |
| 12.13 | No sensors in range | Boot with no TPMS sensors nearby | TPMS gauge shows dashes or no data, no crash | [ ] |

---

## 13. TPMS BATTERY ALARM ON IGNITION

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 13.1 | Battery alarm trigger | Have TPMS sensor(s) with battery <2%, cycle ignition OFF → ON, wait 15s | tirebat.mp3 plays, auto-switches to TPMS gauge | [ ] |
| 13.2 | 15-second delay | Cycle ignition ON, monitor logs | Battery check occurs exactly ~15s after ignition ON (waiting for BLE data) | [ ] |
| 13.3 | No alarm if batteries OK | All sensors >2% battery, cycle ignition ON, wait 15s | No alarm, no switch, no MP3 | [ ] |
| 13.4 | One-shot per ignition | Trigger battery alarm, return to Clock, wait >15s | No second alarm (one-shot per ignition cycle) | [ ] |
| 13.5 | 1-hour cooldown | Trigger battery alarm, cycle ignition OFF → ON within 1 hour | No alarm on second ignition cycle (cooldown active) | [ ] |
| 13.6 | Cooldown expiry | Wait >1 hour after alarm, cycle ignition OFF → ON | Battery alarm triggers again | [ ] |
| 13.7 | No sensors present | No TPMS sensors in range, cycle ignition ON, wait 15s | No alarm, no crash (ble_tpms_any_sensor_present returns false) | [ ] |

---

## 14. TILT GAUGE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 14.1 | Level display | Gauge on flat surface | Shows ~0° (within calibration offset), vehicle image level | [ ] |
| 14.2 | Tilt display | Tilt gauge to ~15° | Vehicle image tilts, angle readout shows ~15° | [ ] |
| 14.3 | Yellow warning — 30° | Tilt gauge to ≥30° | Colour turns yellow, warnroll.mp3 plays, warning beep starts | [ ] |
| 14.4 | Red danger — 35° | Tilt gauge to ≥35° | Colour turns red, dangroll.mp3 plays, faster beeps | [ ] |
| 14.5 | Beep interval — yellow | Hold at 30–34° | Beep every ~10 seconds | [ ] |
| 14.6 | Beep interval — red | Hold at ≥35° | Beep every ~0.5 seconds | [ ] |
| 14.7 | Tilt auto-switch alarm | Viewing another gauge, tilt to ≥30° | Auto-switches to Tilt gauge | [ ] |
| 14.8 | Alarm return | Return gauge to <30°, wait 30 seconds | Auto-returns to previous gauge | [ ] |
| 14.9 | Zero calibration | On Tilt gauge, tilt to 5°, long-press centre | Current angle set as 0°, readout resets to 0° | [ ] |
| 14.10 | Zero persistence | Zero at 5°, reboot | After reboot, same 5° tilt still reads ~0° | [ ] |
| 14.11 | Tilt highest priority | Tilt ≥30° AND EGT overtemp simultaneously | Tilt wins (priority 1), switches to Tilt not EGT | [ ] |

---

## 15. INCLINE GAUGE (PITCH)

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 15.1 | Level display | Gauge on flat surface | Shows ~0° on scale, vehicle image level, pointers at centre tick | [ ] |
| 15.2 | Nose-up display | Tilt gauge nose-up to ~20° | Vehicle image rotates nose-up, pointers move along arcs | [ ] |
| 15.3 | Nose-down display | Tilt gauge nose-down to ~20° | Vehicle image rotates nose-down, pointers move opposite direction | [ ] |
| 15.4 | Scale range ±45° | Tilt to >45° | Pointers clamp at 45° mark, no overshoot past arc limits | [ ] |
| 15.5 | Damping filter | Quickly change pitch angle | Display moves smoothly toward new value, no sudden jumps | [ ] |
| 15.6 | Mode cycle — degrees | Long-press centre on Incline gauge | Scale shows degree marks: 15, 30, 45 | [ ] |
| 15.7 | Mode cycle — 1:X gradient | Long-press again | Scale shows gradient marks: 1:3, 1:2, 1:1 | [ ] |
| 15.8 | Mode cycle — % slope | Long-press again | Scale shows percent marks: 25%, 50%, 75%, 100% | [ ] |
| 15.9 | Mode wraps to degrees | Long-press again (4th time) | Returns to degrees mode: 15, 30, 45 | [ ] |
| 15.10 | Mode persistence | Set to % slope, reboot | Still shows % slope after reboot | [ ] |
| 15.11 | Night mode — image swap | Toggle lights ON/OFF while viewing Incline | Vehicle image swaps to dark variant, scale colour changes to green | [ ] |
| 15.12 | Day mode — image swap | Toggle lights OFF while in night mode | Vehicle image swaps to day variant, scale colour changes to white | [ ] |
| 15.13 | Pointer tracks angle | Slowly pitch from -30° to +30° | Both top and bottom pointers track smoothly along their arcs | [ ] |
| 15.14 | Labels don't overlap | View scale in % slope mode | 25%, 50%, 75%, 100% labels all legible, no text clipping or overlap | [ ] |
| 15.15 | Labels don't wrap | View 100% label | "100%" displays on a single line, % does not wrap | [ ] |
| 15.16 | Image rotation quality | Pitch to ~30° | Vehicle image anti-aliased, no visible jagged edges | [ ] |

---

## 16. COMPASS GAUGE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 16.1 | Heading display | Gauge stationary | Shows stable heading with compass rose, cardinal markers, digital readout | [ ] |
| 16.2 | Rotation tracking | Slowly rotate gauge 360° | Compass card rotates smoothly, heading value increments through 0–360° | [ ] |
| 16.3 | Cardinal directions | Point gauge N, E, S, W | Readout shows correct cardinal (verify against known reference) | [ ] |
| 16.4 | Calibration enter | Long-press centre on Compass gauge | "CALIBRATING" overlay appears | [ ] |
| 16.5 | Calibration rotate | In calibration mode, rotate gauge through full 360° | Completion arc fills as rotation progresses | [ ] |
| 16.6 | Calibration exit | Long-press centre again | "CAL DONE" appears briefly, calibration saved, heading corrected | [ ] |
| 16.7 | Calibration persistence | Calibrate, reboot | Calibration data persists, heading still accurate after reboot | [ ] |
| 16.8 | No magnetometer | Disconnect expansion board or LIS3MDL | Compass gauge skipped in cycle | [ ] |

---

## 17. CLOCK & NTP

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 17.1 | Clock display | Boot and view Clock gauge | Analogue clock with correct time from RTC | [ ] |
| 17.2 | Time accuracy | Compare displayed time against a reference clock | Time accurate within a few seconds | [ ] |
| 17.3 | Manual NTP sync | On Clock gauge, long-press centre | WiFi connects to iPhone hotspot, NTP syncs, RTC updated, WiFi disconnects | [ ] |
| 17.4 | NTP sync visual feedback | During NTP sync | Log shows connection status and sync result | [ ] |
| 17.5 | NTP sync failure — no WiFi | Long-press with no WiFi network available | Timeout after ~15s connect attempt, graceful failure | [ ] |
| 17.6 | Auto NTP sync | Home WiFi in range, >24h since last sync | Automatic sync occurs in background | [ ] |
| 17.7 | 24-hour cooldown | Sync time, then immediately try again via long-press | Manual sync should still work (force override bypasses cooldown) | [ ] |
| 17.8 | Day/night clock rendering | Toggle lights ON/OFF while viewing Clock | Clock renders correctly in both day and night modes | [ ] |

---

## 18. AUDIO SYSTEM

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 18.1 | Confirmation beep | Long-press select button on any action gauge | Short two-tone beep sounds | [ ] |
| 18.2 | MP3 playback — wading | Toggle wading on/off | wadeOn.mp3 / wadeOff.mp3 play correctly | [ ] |
| 18.3 | MP3 playback — fan low | Toggle fan low override on/off | fanLowOn.mp3 / flooff.mp3 play correctly | [ ] |
| 18.4 | MP3 playback — fan high | Toggle fan high override on/off | fhion.mp3 / fhioff.mp3 play correctly | [ ] |
| 18.5 | MP3 playback — coolant low | Trigger confirmed coolant low alarm | coollow.mp3 plays | [ ] |
| 18.6 | MP3 playback — overheat | Trigger 115°C overtemp | overheat.mp3 plays | [ ] |
| 18.7 | MP3 playback — tilt warning | Tilt to ≥30° | warnroll.mp3 plays | [ ] |
| 18.8 | MP3 playback — tilt danger | Tilt to ≥35° | dangroll.mp3 plays | [ ] |
| 18.9 | MP3 playback — TPMS pressure | Trigger pressure drop alarm | tirewar.mp3 plays | [ ] |
| 18.10 | MP3 playback — TPMS battery | Trigger battery alarm on ignition ON | tirebat.mp3 plays | [ ] |
| 18.11 | No SD card — audio graceful fail | Remove SD card, trigger an alert | No crash, audio silently fails | [ ] |

**Required MP3 files on SD card (8.3 format):**

| File | Purpose | Present? |
|------|---------|----------|
| wadeOn.mp3 | Wading mode ON | [ ] |
| wadeOff.mp3 | Wading mode OFF | [ ] |
| fanLowOn.mp3 | Fan low override ON | [ ] |
| flooff.mp3 | Fan low override OFF | [ ] |
| fhion.mp3 | Fan high override ON | [ ] |
| fhioff.mp3 | Fan high override OFF | [ ] |
| coollow.mp3 | Coolant level low | [ ] |
| overheat.mp3 | Coolant overtemperature | [ ] |
| warnroll.mp3 | Tilt warning (30°+) | [ ] |
| dangroll.mp3 | Tilt danger (35°+) | [ ] |
| egtwar.mp3 | EGT warning (680°C) | [ ] |
| egtdang.mp3 | EGT danger (750°C) | [ ] |
| tirewar.mp3 | Tire pressure warning | [ ] |
| tirebat.mp3 | TPMS battery low | [ ] |

---

## 19. ALARM PRIORITY & INTERACTION

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 19.1 | Tilt highest priority | Simultaneously trigger tilt ≥30° and fan low | Switches to Tilt (priority 1), not Cooling (priority 3) | [ ] |
| 19.2 | EGT > Cooling priority | Simultaneously trigger EGT overtemp and coolant low | Switches to EGT (priority 2), not Cooling (priority 3) | [ ] |
| 19.3 | Manual switch lockout | Auto-switch to alarm gauge, immediately tap right to change | 5-second lockout allows manual nav; no immediate auto-switch back | [ ] |
| 19.4 | Alarm return 30s | Auto-switch to Tilt, bring back to <30° | After exactly ~30 seconds, returns to previous gauge | [ ] |
| 19.5 | Alarm re-trigger | Return to <30°, wait 25s (not yet returned), then go back to ≥30° | Return timer resets, stays on Tilt | [ ] |
| 19.6 | Manual nav cancels auto-return | Auto-switched to alarm gauge, manually navigate away | alarm_auto_switched cleared, no auto-return occurs | [ ] |
| 19.7 | Multiple alarms clear | Trigger tilt and cooling alarms, clear tilt first | Stays on tilt until cleared, then returns (won't jump to cooling since it's lower priority and would trigger on next alarm check cycle) | [ ] |

---

## 20. EXPANSION BOARD EDGE CASES

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 20.1 | Board disconnect at runtime | Unplug expansion board I2C while running | Detects board loss after 3 failed I2C reads, expansion gauges skipped | [ ] |
| 20.2 | Board reconnect | Reconnect expansion board after disconnect | Re-detects board, expansion gauges available again | [ ] |
| 20.3 | All inputs HIGH simultaneously | Set all 6 inputs HIGH at once | Each input handled correctly, no interference | [ ] |
| 20.4 | All inputs LOW | Set all inputs LOW | All states show inactive/normal | [ ] |
| 20.5 | Input debounce | Rapidly toggle fan low input | Input state only changes after 50ms stable reading | [ ] |
| 20.6 | Output verification — all relays | Activate wading + fan overrides in sequence | GPA2, GPA3, GPA4 each go HIGH independently | [ ] |

---

## 21. NVS SETTINGS PERSISTENCE

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 21.1 | Boost units survive reboot | Set to PSI, reboot | Shows PSI | [ ] |
| 21.2 | EGT units survive reboot | Set to °F, reboot | Shows °F | [ ] |
| 21.3 | TPMS mode survives reboot | Set to PSI/°F (mode 3), reboot | Shows PSI/°F | [ ] |
| 21.4 | Tilt offset survives reboot | Zero at 5°, reboot at same tilt | Still reads ~0° | [ ] |
| 21.5 | Incline mode survives reboot | Set to % slope, reboot | Still shows % slope scale markings | [ ] |
| 21.6 | Compass cal survives reboot | Calibrate compass, reboot | Heading still corrected, no recalibration needed | [ ] |
| 21.7 | NTP timestamp survives reboot | Sync time, reboot, check auto-sync behaviour | 24-hour cooldown still honoured | [ ] |
| 21.8 | NVS erase recovery | Erase NVS flash, reboot | All settings return to defaults, no crash | [ ] |

---

## 22. STRESS & EDGE CASES

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 22.1 | Rapid gauge switching | Tap left/right rapidly for 30 seconds | No crash, no display corruption, smooth transitions | [ ] |
| 22.2 | Rapid input toggling | Toggle fan low/high rapidly via test jig | No crash, states settle to correct final value | [ ] |
| 22.3 | Long-running stability | Leave gauge running for 1+ hour with ignition ON | No memory leak, no watchdog reset, display stays clean | [ ] |
| 22.4 | Ignition cycling | Toggle ignition ON/OFF 20 times rapidly | Clean standby/wake transitions every time | [ ] |
| 22.5 | Simultaneous touch + button | Press physical button and touch screen within 100ms | One input processed, no conflict or crash | [ ] |
| 22.6 | Multiple MP3 rapid triggers | Trigger multiple different audio alerts in quick succession | Audio plays sequentially or latest wins, no crash | [ ] |
| 22.7 | BLE + WiFi coexistence | Trigger NTP sync while BLE TPMS is scanning | Both complete without interference (or one pauses gracefully) | [ ] |

---

## 23. CUSTOM VEHICLE IMAGES (SD CARD)

| # | Test | Steps | Expected Result | Status |
|---|------|-------|-----------------|--------|
| 23.1 | All 8 custom images | Place all 8 .bin files on SD card, boot | Log shows 8 of 8 loaded; Tilt, Incline, TPMS, Clock show custom images | [ ] |
| 23.2 | Partial custom images | Place only rear.bin on SD card, boot | Tilt day uses custom image, all others use built-in defaults | [ ] |
| 23.3 | Day/night pair | Place rear.bin + reardark.bin, toggle lights | Day mode shows custom rear, night mode shows custom rear dark | [ ] |
| 23.4 | Fallback on missing file | Remove side.bin from SD card, boot | Incline day uses built-in default, no error | [ ] |
| 23.5 | Corrupt .bin file | Place a truncated or invalid .bin file on SD card | Log shows warning, built-in default used, no crash | [ ] |
| 23.6 | Wrong header format | Place .bin with incorrect color format in header | Log shows "invalid header", built-in default used | [ ] |
| 23.7 | No SD card fallback | Boot without SD card | Log shows "No SD card — using built-in images", all gauges display normally | [ ] |
| 23.8 | PSRAM allocation | Load all 6 custom images | No PSRAM allocation failure, all images display correctly | [ ] |
| 23.9 | Image display quality | View each gauge with custom images | Images display correctly, no colour artifacts, transparency works | [ ] |
| 23.10 | SD card removal after boot | Boot with custom images, remove SD card while running | Custom images continue to display (loaded into PSRAM) | [ ] |

---

## Test Execution Log

| Date | Tester | Firmware Version | Tests Run | Pass | Fail | Notes |
|------|--------|-----------------|-----------|------|------|-------|
| | | | | | | |

---

## Notes

- **EGT testing** requires a heat source on the thermocouple or temporarily lowered thresholds in code
- **TPMS battery testing** requires a sensor with genuinely low battery, or modifying the threshold temporarily
- **Pressure drop alarm** requires deflating a tire with a TPMS sensor attached, or simulating rapid BLE data changes
- **10-minute inactivity timeout** — consider temporarily shortening to 1 minute for faster testing, then restoring
- **5-minute fan override timeout** — similarly, consider temporarily shortening for testing
- **1-hour TPMS battery cooldown** — temporarily shorten for practical testing
- All MP3 filenames must be **8.3 format** (≤8 characters before .mp3) due to `CONFIG_FATFS_LFN_NONE=y`
- **Custom image .bin files** can be generated with `python3 convert_image.py --bin` — copy the `sdcard/*.bin` files to the root of the SD card
