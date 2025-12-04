# Artificial Horizon Gauge

An aircraft-style attitude indicator (artificial horizon) for the ESP32-S3-Touch-LCD-2.1, using the onboard QMI8658 6-axis IMU (gyroscope and accelerometer).

## Features

- **Real-time attitude display** showing pitch and roll angles
- **Sky/Ground split** with blue sky and brown earth
- **Pitch ladder** with lines every 10 degrees
- **Roll scale** at the top with angle markings (0°, ±10°, ±20°, ±30°, ±45°, ±60°)
- **Roll pointer** showing current bank angle
- **Fixed aircraft symbol** (yellow wings) at center
- **Day/Night mode** support (white or green instruments)
- **Complementary filter** for smooth, accurate attitude estimation

## Files

- `artificial_horizon.h` - Main header file with API
- `artificial_horizon.c` - Display implementation using LVGL
- `artificial_horizon_example.c` - Example integration with QMI8658 IMU

## How It Works

### Attitude Calculation

The artificial horizon uses a **complementary filter** to combine:
1. **Gyroscope data** - Fast response, integrates angular rates
2. **Accelerometer data** - Long-term stability, measures gravity vector

This approach provides smooth, accurate pitch and roll angles without drift.

### Coordinate System

- **Pitch**: Rotation around Y axis (-90° to +90°)
  - Positive = Nose up
  - Negative = Nose down
  
- **Roll**: Rotation around X axis (-180° to +180°)
  - Positive = Right wing down
  - Negative = Left wing down

## Usage

### Basic Setup

```c
#include "artificial_horizon.h"

// In your initialization code:
artificial_horizon_init();
```

### Update with IMU Data

```c
#include "artificial_horizon_example.c"

// Option 1: Use the example complementary filter
void app_main(void) {
    // ... other initialization ...
    
    horizon_example_init();
    
    // Create task to update horizon at 100Hz
    xTaskCreate(horizon_update_task, "Horizon", 4096, NULL, 5, NULL);
}

// Option 2: Manual update with your own attitude calculation
float pitch = ...; // Your pitch calculation
float roll = ...;  // Your roll calculation
artificial_horizon_update(pitch, roll);
```

### Day/Night Mode

```c
// Switch to night mode (green instruments)
artificial_horizon_set_night_mode(true);

// Switch to day mode (white instruments)
artificial_horizon_set_night_mode(false);
```

### Show/Hide

```c
// Hide the horizon
artificial_horizon_set_visible(false);

// Show the horizon
artificial_horizon_set_visible(true);
```

## Integration Example

```c
#include "artificial_horizon.h"
#include "QMI8658.h"

void app_main(void)
{
    // Initialize hardware
    I2C_Init();
    QMI8658_Init();
    LCD_Init();
    LVGL_Init();
    
    // Initialize artificial horizon
    artificial_horizon_init();
    
    // Main loop
    while (1) {
        // QMI8658 data is updated by QMI8658_Loop()
        // Use the example function to calculate attitude
        horizon_update_attitude();
        
        vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz update
        lv_timer_handler();
    }
}
```

## Tuning Parameters

In `artificial_horizon_example.c`:

- **ALPHA**: Complementary filter weight (default 0.98)
  - Higher = More gyro, faster response, may drift
  - Lower = More accel, more stable, slower response
  
- **DT**: Sample time in seconds (default 0.01 for 100Hz)
  - Must match your actual update rate

In `artificial_horizon.c`:

- **pixels_per_degree**: Pitch sensitivity (default 3.0)
  - Higher = More pitch movement on screen
  - Lower = Less sensitive

## Display Layout

```
     ┌─────────────────────────┐
     │    Roll Scale (arc)     │
     │         ▼ 0°            │
     │   Sky (blue)            │
     │   ─ ─ ─  +10°          │
     │   ─────  Horizon        │
     │   ─ ─ ─  -10°          │
  ───┼───  Aircraft Symbol     │
     │   Ground (brown)        │
     │                         │
     └─────────────────────────┘
```

## Axis Orientation Notes

The QMI8658 sensor orientation on the board may require axis adjustments. If the horizon moves in the wrong direction:

1. **Swap axes**: Try using different gyro/accel components
2. **Invert axes**: Multiply by -1 if movement is reversed
3. **Check sensor datasheet**: Verify the physical orientation

Common adjustments in `horizon_update_attitude()`:
```c
// Example: If pitch/roll are swapped
pitch_angle += Gyro.x * DT;  // Changed from Gyro.y
roll_angle += Gyro.y * DT;   // Changed from Gyro.x

// Example: If direction is inverted
pitch_angle += -Gyro.y * DT;  // Added negative sign
```

## Troubleshooting

**Horizon drifts over time:**
- Decrease ALPHA (try 0.95 or 0.90)
- Ensure QMI8658_Loop() is running regularly
- Check for IMU calibration issues

**Horizon is too sensitive/not sensitive enough:**
- Adjust `pixels_per_degree` in artificial_horizon.c
- For pitch: Change line ~75
- For display scale: Modify HORIZON_SIZE

**Horizon moves in wrong direction:**
- Check axis assignments in `horizon_update_attitude()`
- Verify sensor mounting orientation
- Try inverting axes (multiply by -1)

**Display is choppy:**
- Increase update rate (reduce DT, increase task frequency)
- Ensure lv_timer_handler() is called regularly
- Check LVGL performance settings

## Technical Details

- **Display size**: 480x480 pixels
- **Horizon diameter**: 440 pixels
- **Update rate**: Recommended 50-100 Hz
- **Memory**: Uses LVGL canvas for drawing
- **Filter type**: First-order complementary filter
- **Pitch range**: ±90 degrees
- **Roll range**: ±180 degrees

## Future Enhancements

Possible additions:
- Slip/skid indicator (ball)
- Turn rate indicator
- Altimeter integration
- Heading display
- Quaternion-based attitude (for full 3D)
- Kalman filter for better accuracy
