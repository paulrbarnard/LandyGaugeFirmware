# Hardware Variant Support

This project automatically supports both **Touch** and **Non-Touch** versions of the Waveshare ESP32-S3-Touch-LCD-1.85 board with a single firmware binary.

## Self-Configuring Design

**No configuration required!** The firmware automatically detects the hardware at startup:

1. Attempts to initialize the CST820 touch controller via I2C
2. If touch controller responds → enables touch input
3. If touch controller not found → runs in button-only mode
4. Button input is always available on both versions

### Boot Log Examples

**Touch Version:**
```
I (xxx) cst820: Touch controller CST820 detected and initialized successfully
I (xxx) MAIN: Touch screen detected and enabled
I (xxx) LVGL: Touch input device registered
```

**Non-Touch Version:**
```
W (xxx) cst820: I2C probe failed - no response from CST820 touch controller
I (xxx) MAIN: No touch screen detected - button-only mode
```

## Quick Start

Just build and flash - same binary works on both hardware versions:

```bash
idf.py build flash monitor
```

## Hardware Differences

| Feature | Touch Version | Non-Touch Version |
|---------|---------------|-------------------|
| Touch Controller | CST820 (I2C) | None (auto-detected) |
| Touch Input | ✅ Enabled | ❌ Disabled |
| Button Input | ✅ Always works | ✅ Always works |
| Gauge Switching | Touch or Button | Button only |
| Firmware Binary | Same | Same |

## Input Methods

### Touch Input (Touch Version Only)
- Tap anywhere on the screen to cycle to the next gauge
- Touch overlay covers full screen

### Button Input (Both Versions)
- Press the **boot button (GPIO0)** to cycle to the next gauge
- 50ms debounce for reliable detection
- Active low (press connects GPIO to GND)

## Button Configuration

You can customize the button GPIO and debounce time in menuconfig:

```bash
idf.py menuconfig
```

Navigate to: **Example Configuration**
- **Next Gauge Button GPIO**: GPIO pin number (default: 0 = boot button)
- **Button Debounce Time (ms)**: Debounce delay (default: 50ms)

### Button Wiring

Buttons should connect GPIO to GND when pressed:
- Internal pull-up resistor is enabled automatically
- Triggering on button release (after debounce)

```
[Button] ---- GPIO 0 ---- [Internal Pull-Up to 3.3V]
    |
   GND
```

### Recommended GPIO Pins

Safe GPIO pins for buttons on ESP32-S3:
- **GPIO 0**: Boot button (already present on dev boards)
- **GPIO 4, 5, 6**: General purpose
- **GPIO 16, 17, 18**: General purpose
- **Avoid**: LCD pins, I2C (1, 3), SPI, JTAG

## Current Features

### Implemented Gauges
1. **Analog Clock** - WiFi/NTP synchronized
2. **Artificial Horizon** - IMU-based attitude indicator

### Gauge Switching
- **Touch Version**: Tap screen OR press button
- **Non-Touch Version**: Press button only
- Gauges cycle: Clock → Horizon → Clock → ...

## Technical Details

### Runtime Detection

The touch controller detection happens in `Touch_Init()`:

```c
// CST820.c
bool Touch_Init(void) {
    // Try to communicate with CST820
    esp_err_t ret = i2c_master_probe(i2c_master_handle, CST820_ADDR, 100);
    if (ret != ESP_OK) {
        touch_available = false;
        return false;  // No touch controller
    }
    touch_available = true;
    return true;  // Touch enabled
}
```

Main code uses the flag for conditional behavior:

```c
// main.c
extern bool touch_available;

if (touch_available) {
    // Create touch overlay for gesture detection
    create_touch_overlay();
}
```

### Files Structure

**Always Compiled:**
- `Touch_Driver/CST820.c` - Always included, detection at runtime
- `Button_Driver/button_input.c` - Always included

**Runtime Conditional:**
- Touch input device registration in LVGL
- Touch overlay creation in main.c

## Troubleshooting

### Buttons Not Working

1. Check GPIO configuration in menuconfig
2. Verify button wiring (should connect GPIO to GND when pressed)
3. Monitor serial output for button initialization:
   ```
   I (xxx) button_input: Button input initialized on GPIO0
   ```
4. Check that GPIO is not used by other peripherals

### Touch Not Working (Touch Hardware)

1. Check serial output for detection result:
   ```
   I (xxx) cst820: Touch controller CST820 detected...
   ```
2. If showing "I2C probe failed":
   - Verify CST820 is properly connected via I2C
   - Check I2C pins: SDA=GPIO1, SCL=GPIO3
   - Ensure TCA9554PWR (EXIO) is initialized first for touch reset
3. If detected but not responding to touches:
   - Check touch interrupt GPIO
   - Verify LVGL input device registration message

### General Debug

Enable verbose logging for touch detection:
```c
esp_log_level_set("cst820", ESP_LOG_DEBUG);
```
