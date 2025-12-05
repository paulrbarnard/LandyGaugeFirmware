# Hardware Variant Configuration Guide

This project supports both **Touch** and **Non-Touch** versions of the Waveshare ESP32-S3-Touch-LCD-1.85 board.

## Quick Start

### Touch Version (Default)
The project is configured for the touch version by default. No changes needed - just build and flash:

```bash
idf.py build flash monitor
```

### Non-Touch Version
To build for the non-touch version with GPIO buttons:

```bash
idf.py menuconfig
```

Navigate to: **Example Configuration → Hardware Variant**
- Select: **"Non-Touch Version (with external buttons)"**
- Save and exit (S, then Enter)

Then build and flash:

```bash
idf.py build flash monitor
```

## Hardware Differences

| Feature | Touch Version | Non-Touch Version |
|---------|---------------|-------------------|
| Touch Controller | CST820 (I2C) | None |
| Gauge Switching | Touch gestures* | GPIO buttons |
| Default Buttons | N/A | GPIO 0 (Next), GPIO 4 (Prev) |
| Binary Size | ~1.63 MB | ~1.62 MB |

*Touch gesture support to be implemented

## GPIO Button Configuration

For the non-touch version, you can customize the button GPIO pins:

```bash
idf.py menuconfig
```

Navigate to: **Example Configuration**
- **Next Button GPIO**: Set the GPIO pin for "Next Gauge" button (default: 0)
- **Previous Button GPIO**: Set the GPIO pin for "Previous Gauge" button (default: 4)

### Button Wiring

Connect buttons between GPIO pin and GND:
- Internal pull-up resistors are enabled automatically
- Buttons should short GPIO to GND when pressed
- Triggering on falling edge (button press)

Example for default configuration:
```
[Button Next]  ---- GPIO 0 ---- [Internal Pull-Up]
     |
    GND

[Button Prev]  ---- GPIO 4 ---- [Internal Pull-Up]
     |
    GND
```

### Recommended GPIO Pins

Safe GPIO pins for buttons on ESP32-S3:
- **GPIO 0**: Boot button (often available on dev boards)
- **GPIO 4, 5, 6**: General purpose
- **GPIO 16, 17, 18**: General purpose
- **Avoid**: GPIO used by LCD (see ST7701S.h), I2C (7, 15), SPI (1, 2)

## Current Features

### Implemented Gauges
1. **Analog Clock** - WiFi/NTP synchronized
2. **Artificial Horizon** - IMU-based attitude indicator

### Gauge Switching

**Touch Version (Future):**
- Swipe left/right to change gauges
- Tap for settings/options

**Non-Touch Version (Current):**
- Press "Next" button to advance to next gauge
- Press "Prev" button to go to previous gauge
- Gauges wrap around (Clock ↔ Horizon)

## Technical Details

### Conditional Compilation

The project uses ESP-IDF's Kconfig system for conditional compilation:

```c
#ifdef CONFIG_USE_TOUCH_CONTROLLER
    // Touch-specific code (CST820 driver)
    Touch_Init();
#else
    // Non-touch code (GPIO button handlers)
    button_init();
#endif
```

### Files Affected

**Conditionally Compiled:**
- `Touch_Driver/CST820.c` - Only compiled for touch version
- `Touch_Driver/esp_lcd_touch/esp_lcd_touch.c` - Only compiled for touch version

**Common Files:**
- All other drivers (LCD, IMU, RTC, etc.) are shared
- Gauge implementations work on both versions

## Building Both Versions

To build binaries for both versions:

### Touch Version:
```bash
idf.py menuconfig  # Select "Touch Version"
idf.py build
cp build/ESP32-S3-Touch-LCD-1.85.bin ESP32-S3-Touch-LCD-1.85_TOUCH.bin
```

### Non-Touch Version:
```bash
idf.py menuconfig  # Select "Non-Touch Version"
idf.py fullclean build
cp build/ESP32-S3-Touch-LCD-1.85.bin ESP32-S3-Touch-LCD-1.85_NO_TOUCH.bin
```

## Troubleshooting

### Build Errors After Switching Versions

If you encounter build errors after changing hardware variant:

```bash
idf.py fullclean
idf.py reconfigure
idf.py build
```

### Buttons Not Working (Non-Touch)

1. Verify GPIO pins in menuconfig match your hardware
2. Check button wiring (should connect GPIO to GND)
3. Verify GPIOs are not used by other peripherals
4. Monitor serial output for button initialization messages

### Touch Not Working (Touch Version)

1. Verify CST820 is properly connected via I2C
2. Check I2C pins: SDA=GPIO15, SCL=GPIO7
3. Monitor for touch initialization messages
4. Check touch interrupt GPIO configuration
