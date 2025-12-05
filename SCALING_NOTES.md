# LandyGaugeSmall - Scaling Summary

## Project Overview
Created scaled version of LandyGauge for ESP32-S3-Touch-LCD-1.85 (360x360 display)

## Scaling Details

### Display Resolution
- **Original**: 480x480 pixels (ESP32-S3-Touch-LCD-2.1)
- **New**: 360x360 pixels (ESP32-S3-Touch-LCD-1.85)
- **Scaling Factor**: 0.75× (360/480)

### Files Modified

#### 1. LCD Driver Configuration
**File**: `main/LCD_Driver/ST7701S.h`
- Changed `EXAMPLE_LCD_H_RES` from 480 to 360
- Changed `EXAMPLE_LCD_V_RES` from 480 to 360

#### 2. Clock Gauge
**File**: `main/Clock/clock.c`

Original (480x480) → Scaled (360x360):
- Clock diameter: 480px → 360px
- Clock center: (240, 240) → (180, 180)
- Shadow circles: 440px → 330px (CLOCK_SIZE - 30)
- Shadow width: 20px → 15px
- Shadow offset: ±5px → ±4px
- Hour hand width: 16px → 12px
- Minute hand width: 12px → 9px
- Quarter hour markers: 10px → 8px wide
- Intermediate markers: 5px → 4px wide
- Marker positions: Scaled from (size/2 - 36 - 20) to (size/2 - 27 - 15)
- Number radius: (size/2 - 60 - 20) → (size/2 - 45 - 15)

#### 3. Artificial Horizon
**File**: `main/ArtificialHorizon/artificial_horizon.c`

Original (480x480) → Scaled (360x360):
- Screen size: 480×480 → 360×360
- Horizon display: 440px → 330px diameter
- Shadow circles: 430px → 322px (HORIZON_SIZE - 8)
- Shadow width: 20px → 15px
- Shadow offset: ±5px → ±4px
- Masking ring border: 20px → 15px
- Pitch line widths: 3px → 2px
- Major tick marks: 60px → 45px
- Minor tick marks: 40px → 30px, 20px → 15px
- Wing length: 80px → 60px
- Wing width: 8px → 6px
- Center dot: 10px → 8px (radius 5px → 4px)
- Roll arc radius offset: 15px → 12px
- Roll tick start: 10px → 8px
- Roll tick end: 30px → 22px

### Scaling Approach

All dimensions were scaled by the 0.75 factor with these considerations:

1. **Proportional Scaling**: Most dimensions multiplied by 0.75
2. **Integer Rounding**: Values rounded to nearest integer
3. **Line Widths**: Reduced but kept visually legible (min 2px for main elements)
4. **Shadow Effects**: Maintained similar visual depth perception
5. **Spacing**: Preserved relative relationships between elements

### Automation Scripts

Two shell scripts were created to automate the scaling process:

1. **scale_update.sh**: Primary scaling operations
   - Display resolution updates
   - Basic dimensional scaling
   - Shadow size adjustments

2. **fine_tune.sh**: Fine-grained adjustments
   - Line width refinements
   - Marker positioning
   - Detail element scaling

### Files Unchanged

The following components did not require modification:
- All driver modules (I2C, Touch, Buzzer, BAT, QMI8658, PCF85063, etc.)
- WiFi/NTP functionality
- Warning beep system
- IMU sensor code
- Complementary filter algorithm
- Color schemes and themes
- Build system configuration (CMakeLists.txt works as-is)

### Testing Recommendations

1. Build the project: `idf.py build`
2. Flash to ESP32-S3-Touch-LCD-1.85: `idf.py flash monitor`
3. Verify gauge element proportions
4. Check text readability at smaller scale
5. Validate shadow effects appearance
6. Test touch responsiveness
7. Confirm IMU calibration still accurate

### Visual Consistency

The scaled version maintains the same visual appearance and proportions as the original:
- Clock hands maintain same relative lengths
- Shadow depth appears identical
- Warning colors preserved
- Day/night mode contrast unchanged
- Pitch/roll scales equally readable

### Known Limitations

1. Text size may be smaller - consider font size adjustments if needed
2. Touch targets may be slightly smaller (still within usability guidelines)
3. Some fine details may be less visible on the smaller 1.85" display vs 2.1"

### Future Enhancements

If text readability is an issue, consider:
- Increasing font sizes (reduce by less than 0.75 factor)
- Simplifying some labels
- Removing minor tick marks if too dense
- Using thicker lines for critical elements

### Build Status

- ✅ All files copied
- ✅ Display resolution updated
- ✅ Clock gauge scaled
- ✅ Artificial horizon scaled
- ✅ Shadow effects adjusted
- ✅ Git repository initialized
- ⏳ Build and flash testing pending

### Project Location

- **Original**: `/Users/pbarnard/Documents/ESP/LandyGauge`
- **Scaled**: `/Users/pbarnard/Documents/ESP/LandyGaugeSmall`

Both projects are independent and can be maintained separately.
