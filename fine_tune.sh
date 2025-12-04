#!/bin/bash
# Fine-tune scaling for line widths and other details

cd /Users/pbarnard/Documents/ESP/LandyGaugeSmall

echo "Scaling line widths in Clock..."
# Scale hand widths: 16->12, 12->9
sed -i '' 's/lv_obj_set_style_line_width(hour_hand, 16, 0)/lv_obj_set_style_line_width(hour_hand, 12, 0)/' main/Clock/clock.c
sed -i '' 's/lv_obj_set_style_line_width(minute_hand, 12, 0)/lv_obj_set_style_line_width(minute_hand, 9, 0)/' main/Clock/clock.c

# Scale marker widths: 10->8 (quarter), 5->4 (intermediate)
sed -i '' 's/line_width = 10;  \/\/ Quarter hour markers/line_width = 8;  \/\/ Quarter hour markers/' main/Clock/clock.c
sed -i '' 's/line_width = 5;  \/\/ Intermediate hour markers/line_width = 4;  \/\/ Intermediate hour markers/' main/Clock/clock.c

echo "Scaling font sizes and dimensions in Artificial Horizon..."
# These will need to be updated in artificial_horizon.c

# Scale pitch ladder line widths from 3 to 2
sed -i '' 's/line_dsc\.width = 3;/line_dsc.width = 2;/' main/ArtificialHorizon/artificial_horizon.c

# Scale tick marks from 60 to 45, 40 to 30, 20 to 15
sed -i '' 's/int line_half_length = 60;/int line_half_length = 45;/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/int line_half_length = 40;/int line_half_length = 30;/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/int line_half_length = 20;/int line_half_length = 15;/' main/ArtificialHorizon/artificial_horizon.c

# Scale wing length from 80 to 60
sed -i '' 's/int wing_length = 80;/int wing_length = 60;/' main/ArtificialHorizon/artificial_horizon.c

# Scale wing width from 8 to 6
sed -i '' 's/line_dsc\.width = 8;  \/\/ Thicker for aircraft symbol/line_dsc.width = 6;  \/\/ Thicker for aircraft symbol/' main/ArtificialHorizon/artificial_horizon.c

# Scale center dot radius from 5 to 4
sed -i '' 's/lv_obj_set_style_radius(center_dot, 5, 0);/lv_obj_set_style_radius(center_dot, 4, 0);/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/lv_obj_set_size(center_dot, 10, 10);/lv_obj_set_size(center_dot, 8, 8);/' main/ArtificialHorizon/artificial_horizon.c

# Scale roll arc dimensions
sed -i '' 's/int arc_radius = radius - 15;/int arc_radius = radius - 12;/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/int tick_start = radius - 10;/int tick_start = radius - 8;/' main/ArtificialHorizon/artificial_horizon.c
sed -i '' 's/int tick_end = radius - 30;/int tick_end = radius - 22;/' main/ArtificialHorizon/artificial_horizon.c

echo "Fine-tuning complete!"
