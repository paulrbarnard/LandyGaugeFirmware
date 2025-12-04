#!/bin/bash
# Script to scale all gauge elements from 480x480 to 360x360
# Scaling factor: 0.75

echo "Updating Clock gauge dimensions..."

# Update Clock gauge
sed -i '' 's/#define CLOCK_SIZE          480/#define CLOCK_SIZE          360/' main/Clock/clock.c
sed -i '' 's/#define CLOCK_CENTER_X      240/#define CLOCK_CENTER_X      180/' main/Clock/clock.c
sed -i '' 's/#define CLOCK_CENTER_Y      240/#define CLOCK_CENTER_Y      180/' main/Clock/clock.c
sed -i '' 's/CLOCK_SIZE - 40/CLOCK_SIZE - 30/g' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_width(shadow_dark, 20, 0)/lv_obj_set_style_shadow_width(shadow_dark, 15, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_width(shadow_light, 20, 0)/lv_obj_set_style_shadow_width(shadow_light, 15, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_x(shadow_dark, -5, 0)/lv_obj_set_style_shadow_ofs_x(shadow_dark, -4, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_y(shadow_dark, -5, 0)/lv_obj_set_style_shadow_ofs_y(shadow_dark, -4, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_x(shadow_light, 5, 0)/lv_obj_set_style_shadow_ofs_x(shadow_light, 4, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_y(shadow_light, 5, 0)/lv_obj_set_style_shadow_ofs_y(shadow_light, 4, 0)/' main/Clock/clock.c

# Scale clock marker positions (480/2 - 36 - 20 = 184 -> 360/2 - 27 - 15 = 138)
sed -i '' 's/marker_start = CLOCK_SIZE \/ 2 - 36 - 20/marker_start = CLOCK_SIZE \/ 2 - 27 - 15/' main/Clock/clock.c
sed -i '' 's/marker_end = CLOCK_SIZE \/ 2 - 5 - 20/marker_end = CLOCK_SIZE \/ 2 - 4 - 15/' main/Clock/clock.c
sed -i '' 's/number_radius = CLOCK_SIZE \/ 2 - 60 - 20/number_radius = CLOCK_SIZE \/ 2 - 45 - 15/' main/Clock/clock.c

# Update line widths for clock
sed -i '' 's/line_width = 4/line_width = 3/' main/Clock/clock.c

echo "Updating Artificial Horizon dimensions..."

# Update Artificial Horizon
sed -i '' 's/#define SCREEN_WIDTH 480/#define SCREEN_WIDTH 360/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/#define SCREEN_HEIGHT 480/#define SCREEN_HEIGHT 360/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/#define HORIZON_SIZE 440/#define HORIZON_SIZE 330/' main/ArtificialHorizon/artificial_horizon.c

# Scale horizon shadows from -10 to -8 pixels
sed -i '' 's/HORIZON_SIZE - 10/HORIZON_SIZE - 8/' main/ArtificialHorizon/artificial_horizon.c

# Scale shadow widths
sed -i '' 's/lv_obj_set_style_shadow_width(shadow_dark, 20, 0)/lv_obj_set_style_shadow_width(shadow_dark, 15, 0)/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/lv_obj_set_style_shadow_width(shadow_light, 20, 0)/lv_obj_set_style_shadow_width(shadow_light, 15, 0)/' main/ArtificialHorizon/artificial_horizon.c

# Scale shadow offsets
sed -i '' 's/lv_obj_set_style_shadow_ofs_x(shadow_dark, -5, 0)/lv_obj_set_style_shadow_ofs_x(shadow_dark, -4, 0)/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_y(shadow_dark, -5, 0)/lv_obj_set_style_shadow_ofs_y(shadow_dark, -4, 0)/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_x(shadow_light, 5, 0)/lv_obj_set_style_shadow_ofs_x(shadow_light, 4, 0)/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/lv_obj_set_style_shadow_ofs_y(shadow_light, 5, 0)/lv_obj_set_style_shadow_ofs_y(shadow_light, 4, 0)/' main/ArtificialHorizon/artificial_horizon.c

# Scale masking ring border width from 20 to 15
sed -i '' 's/lv_obj_set_style_border_width(mask_ring, 20, 0)/lv_obj_set_style_border_width(mask_ring, 15, 0)/' main/ArtificialHorizon/artificial_horizon.c

# Scale line widths in artificial horizon (3->2, 2->1.5≈2, 5->4)
sed -i '' 's/lv_draw_line_dsc_t line_dsc;/lv_draw_line_dsc_t line_dsc;/' main/ArtificialHorizon/artificial_horizon.c

echo "Updating README..."
sed -i '' 's/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-1.85/' README.md
sed -i '' 's/480x480/360x360/g' README.md
sed -i '' 's/LandyGauge/LandyGaugeSmall/' README.md

echo "Scale update complete!"
echo "Please review the changes and rebuild the project."
