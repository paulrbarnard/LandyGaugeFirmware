# LandyGaugeSmall - Vehicle Instrument Panel for ESP32-S3-Touch-LCD-1.85

A scaled version of LandyGauge optimized for the Waveshare ESP32-S3-Touch-LCD-1.85 (360x360 display).

## Hardware Support

This project supports **both touch and non-touch versions** of the Waveshare ESP32-S3-Touch-LCD-1.85 board.

### Hardware Variants

#### Touch Version (Default)
- **Board**: Waveshare ESP32-S3-Touch-LCD-1.85 (Touch)
- **Display**: 1.85" RGB LCD, 360x360 resolution
- **Touch**: CST820 capacitive touch controller
- **Gauge Switching**: Touch gestures (to be implemented)

#### Non-Touch Version
- **Board**: Waveshare ESP32-S3-Touch-LCD-1.85 (Non-Touch)
- **Display**: 1.85" RGB LCD, 360x360 resolution
- **Touch**: None
- **Gauge Switching**: External buttons on GPIO pins
  - Default Next Button: GPIO 0
  - Default Prev Button: GPIO 4
  - **Combo Press**: Pressing both Next + Prev simultaneously (within 150ms) acts as a Select button
  - **Long Press**: Holding the combo press for 1 second triggers a long-press select action

### Common Features
- **IMU**: QMI8658 6-axis motion sensor (gyroscope + accelerometer)
- **MCU**: ESP32-S3 with 8MB Flash, 8MB PSRAM
- **Display**: RGB565 color format, ST7701S LCD driver

## Configuration

Select your hardware variant using menuconfig:

```bash
idf.py menuconfig
```

Navigate to: **Example Configuration → Hardware Variant**

- Select "Touch Version" for boards with CST820 touch controller
- Select "Non-Touch Version" for boards without touch, using GPIO buttons

For non-touch version, you can configure button GPIO pins:
- **Next Button GPIO**: Default GPIO 0
- **Previous Button GPIO**: Default GPIO 4

## Implemented Gauges

### 1. Analog Clock
- WiFi/NTP time synchronization
- UK timezone support (Europe/London)
- Day/night mode with automatic color switching
- Hour markers and numbers
- Second, minute, and hour hands
- 3D shadow effects for recessed appearance

### 2. Artificial Horizon (Attitude Indicator)
- Real-time pitch and roll display using QMI8658 IMU
- Aircraft-style horizon with sky/ground split
- Pitch ladder with 10° increments and numeric labels
- Roll scale arc with degree markings
- Fixed aircraft symbol (yellow wings)
- Complementary filter for smooth attitude estimation
  - Alpha = 0.50 (balanced gyro/accelerometer weighting)
  - 100Hz update rate
- Color-coded warnings:
  - Green/White: Normal operation
  - Yellow: Pitch ±35° or Roll ±30°
  - Red: Pitch ±45° or Roll ±35°
- Audio warning beeps at configurable intervals
- Day/night mode support

## Display Scaling

This version is scaled from the original 480x480 LandyGauge display to 360x360:
- **Scaling factor**: 0.75×
- All gauge elements proportionally scaled
- Shadow widths: 15px (vs 20px original)
- Line widths appropriately reduced
- Maintains visual consistency with larger version

## Global Configuration

- `night_mode_enabled`: Controls day/night color scheme for all gauges
- `current_gauge`: Selects which gauge to display (GAUGE_CLOCK or GAUGE_ARTIFICIAL_HORIZON)

## Building

```bash
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```

## Display Configuration

The display resolution is configured in `main/LCD_Driver/ST7701S.h`:
```c
#define EXAMPLE_LCD_H_RES  360
#define EXAMPLE_LCD_V_RES  360
```

## Gauge Dimensions

### Clock
- Clock diameter: 360px (full screen)
- Shadow circles: 330px (360 - 30)
- Hour markers: Scaled appropriately
- Hand widths: Hour 12px, Minute 9px

### Artificial Horizon
- Screen size: 360×360px
- Horizon display: 330px diameter
- Shadow circles: 322px (330 - 8)
- Masking ring: 15px border
- All line widths and spacing scaled by 0.75×

## Future Enhancements

- Additional gauges (speedometer, tachometer, fuel, temperature)
- Touch-based gauge switching
- OBD-II integration for vehicle data
- GPS speedometer
- Custom warning thresholds

## Credits

Based on LandyGauge project for ESP32-S3-Touch-LCD-2.1 (480x480).
