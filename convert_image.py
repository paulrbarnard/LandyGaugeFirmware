#!/usr/bin/env python3
"""Convert PNG images to LVGL C array format and/or SD card .bin format.

Usage:
  python3 convert_image.py              # Convert all PNGs to C arrays
  python3 convert_image.py --bin        # Also generate SD card .bin files
  python3 convert_image.py --bin-only   # Only generate SD card .bin files
"""

import sys
import struct
from PIL import Image

def convert_to_lvgl_c(input_path, output_path, var_name, attr_name):
    img = Image.open(input_path).convert("RGBA")
    w, h = img.size
    pixels = list(img.getdata())
    data_size = w * h

    with open(output_path, 'w') as f:
        # Header
        f.write('#ifdef __has_include\n')
        f.write('    #if __has_include("lvgl.h")\n')
        f.write('        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n')
        f.write('            #define LV_LVGL_H_INCLUDE_SIMPLE\n')
        f.write('        #endif\n')
        f.write('    #endif\n')
        f.write('#endif\n\n')
        f.write('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n')
        f.write('    #include "lvgl.h"\n')
        f.write('#else\n')
        f.write('    #include "lvgl/lvgl.h"\n')
        f.write('#endif\n\n\n')
        f.write('#ifndef LV_ATTRIBUTE_MEM_ALIGN\n')
        f.write('#define LV_ATTRIBUTE_MEM_ALIGN\n')
        f.write('#endif\n\n')
        f.write(f'#ifndef LV_ATTRIBUTE_IMG_{attr_name}\n')
        f.write(f'#define LV_ATTRIBUTE_IMG_{attr_name}\n')
        f.write('#endif\n\n')
        f.write(f'const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_{attr_name} uint8_t {var_name}_map[] = {{\n')

        # Color depth 1/8 - just output zeros (not used on ESP32-S3 with 16-bit display)
        f.write('#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8\n')
        write_pixel_data(f, pixels, bpp=1)
        f.write('\n#endif\n')

        # Color depth 16
        f.write('#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 1\n')
        write_pixel_data_16(f, pixels, swap=False)
        f.write('\n#endif\n')

        f.write('#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 1\n')
        write_pixel_data_16(f, pixels, swap=True)
        f.write('\n#endif\n')

        # Color depth 32
        f.write('#if LV_COLOR_DEPTH == 32\n')
        write_pixel_data_32(f, pixels)
        f.write('\n#endif\n')

        f.write('};\n\n')

        # Image descriptor
        f.write(f'const lv_img_dsc_t {var_name} = {{\n')
        f.write('  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n')
        f.write('  .header.always_zero = 0,\n')
        f.write('  .header.reserved = 0,\n')
        f.write(f'  .header.w = {w},\n')
        f.write(f'  .header.h = {h},\n')
        f.write(f'  .data_size = {data_size} * LV_IMG_PX_SIZE_ALPHA_BYTE,\n')
        f.write(f'  .data = {var_name}_map,\n')
        f.write('};\n')

    print(f"Converted {input_path} -> {output_path} ({w}x{h}, {data_size} pixels)")


def write_pixel_data(f, pixels, bpp):
    """Write 8-bit color depth pixel data (R,G,B,A as single bytes)."""
    line = '  '
    count = 0
    for r, g, b, a in pixels:
        # Convert to 332 format for 8-bit
        c8 = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)
        line += f'0x{c8:02x}, 0x{a:02x}, '
        count += 1
        if count % 16 == 0:
            f.write(line + '\n')
            line = '  '
    if count % 16 != 0:
        f.write(line + '\n')


def write_pixel_data_16(f, pixels, swap=False):
    """Write 16-bit color depth pixel data (RGB565 + alpha)."""
    line = '  '
    count = 0
    for r, g, b, a in pixels:
        # RGB565
        c16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        if swap:
            lo = c16 & 0xFF
            hi = (c16 >> 8) & 0xFF
            line += f'0x{hi:02x}, 0x{lo:02x}, 0x{a:02x}, '
        else:
            lo = c16 & 0xFF
            hi = (c16 >> 8) & 0xFF
            line += f'0x{lo:02x}, 0x{hi:02x}, 0x{a:02x}, '
        count += 1
        if count % 12 == 0:
            f.write(line + '\n')
            line = '  '
    if count % 12 != 0:
        f.write(line + '\n')


def write_pixel_data_32(f, pixels):
    """Write 32-bit color depth pixel data (B,G,R,A)."""
    line = '  '
    count = 0
    for r, g, b, a in pixels:
        # LVGL 32-bit uses BGRA byte order
        line += f'0x{b:02x}, 0x{g:02x}, 0x{r:02x}, 0x{a:02x}, '
        count += 1
        if count % 10 == 0:
            f.write(line + '\n')
            line = '  '
    if count % 10 != 0:
        f.write(line + '\n')


def convert_to_lvgl_bin(input_path, output_path):
    """Convert PNG to LVGL binary .bin format for SD card loading.

    File format:
      Offset 0: lv_img_header_t (4 bytes, packed bitfield)
      Offset 4: raw pixel data (RGB565 byte-swapped + alpha, 3 bytes/pixel)

    lv_img_header_t (uint32_t, little-endian):
      bits [0:4]   = cf (5 = LV_IMG_CF_TRUE_COLOR_ALPHA)
      bit  [5]     = always_zero
      bits [6:7]   = reserved
      bits [8:18]  = w (11 bits)
      bits [19:29] = h (11 bits)
      bits [30:31] = reserved
    """
    img = Image.open(input_path).convert("RGBA")
    w, h = img.size
    pixels = list(img.getdata())

    LV_IMG_CF_TRUE_COLOR_ALPHA = 5
    header = LV_IMG_CF_TRUE_COLOR_ALPHA | (w << 8) | (h << 19)

    with open(output_path, 'wb') as f:
        f.write(struct.pack('<I', header))
        for r, g, b, a in pixels:
            # RGB565 byte-swapped (LV_COLOR_16_SWAP=1)
            c16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            hi = (c16 >> 8) & 0xFF
            lo = c16 & 0xFF
            f.write(bytes([hi, lo, a]))

    data_size = w * h * 3
    print(f"Converted {input_path} -> {output_path} ({w}x{h}, {data_size + 4} bytes)")


if __name__ == '__main__':
    base = '/Users/pbarnard/Documents/ESP/LandyGaugeSmall'

    conversions = [
        ('images/110Rear235.png',     'main/110Rear235.c',     'rear_110_235',       'REAR_110_235'),
        ('images/110RearDark235.png', 'main/110RearDark235.c', 'rear_dark_110_235',  'REAR_DARK_110_235'),
        ('images/110Roof150w.png',    'main/110Roof150w.c',    'roof_110_150w',      'ROOF_110_150W'),
        ('images/110RoofDark150w.png','main/110RoofDark150w.c','roof_dark_110_150w', 'ROOF_DARK_110_150W'),
        ('images/110Side292w.png',    'main/110Side292w.c',    'side_110_292w',      'SIDE_110_292W'),
        ('images/110SideDark292w.png','main/110SideDark292w.c','side_dark_110_292w', 'SIDE_DARK_110_292W'),
        ('images/logo.png',          'main/logo.c',          'logo_img',           'LOGO_IMG'),
        ('images/logoDark.png',      'main/logoDark.c',      'logo_dark_img',      'LOGO_DARK_IMG'),
    ]

    # SD card .bin output mapping (8.3 filenames)
    bin_conversions = [
        ('images/110Rear235.png',     'sdcard/rear.bin'),
        ('images/110RearDark235.png', 'sdcard/reardark.bin'),
        ('images/110Side292w.png',    'sdcard/side.bin'),
        ('images/110SideDark292w.png','sdcard/sidedark.bin'),
        ('images/110Roof150w.png',    'sdcard/roof.bin'),
        ('images/110RoofDark150w.png','sdcard/roofdark.bin'),
        ('images/logo.png',          'sdcard/logo.bin'),
        ('images/logoDark.png',      'sdcard/logodark.bin'),
    ]

    args = sys.argv[1:]
    do_bin = '--bin' in args or '--bin-only' in args
    do_c   = '--bin-only' not in args

    # Convert to C arrays (default)
    if do_c:
        targets = [a for a in args if not a.startswith('--')] or [c[0] for c in conversions]
        for img_rel, c_rel, var_name, attr_name in conversions:
            if img_rel in targets or 'all' in targets:
                import os
                img_path = os.path.join(base, img_rel)
                c_path = os.path.join(base, c_rel)
                if os.path.exists(img_path):
                    convert_to_lvgl_c(img_path, c_path, var_name, attr_name)
                else:
                    print(f"Skipping {img_rel} (not found)")

    # Convert to SD card .bin files
    if do_bin:
        import os
        os.makedirs(os.path.join(base, 'sdcard'), exist_ok=True)
        for img_rel, bin_rel in bin_conversions:
            img_path = os.path.join(base, img_rel)
            bin_path = os.path.join(base, bin_rel)
            if os.path.exists(img_path):
                convert_to_lvgl_bin(img_path, bin_path)
            else:
                print(f"Skipping {img_rel} (not found)")
        print(f"\nSD card .bin files written to {os.path.join(base, 'sdcard')}/")
        print("Copy the .bin files to the root of your SD card.")
