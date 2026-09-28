"""Convert the supplied Shongket logo BMP to a full-color RGB565 TFT bitmap.

First export the source PNG with `sips -s format bmp source.png --out logo.bmp`.
The generated header is committed so firmware builds do not need image tools.
"""

import struct
import sys
from pathlib import Path


def main():
    data = Path(sys.argv[1]).read_bytes()
    if data[:2] != b"BM":
        raise ValueError("expected BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, signed_height = struct.unpack_from("<ii", data, 18)
    depth = struct.unpack_from("<H", data, 28)[0]
    if depth != 32:
        raise ValueError("expected 32-bit BMP")
    height = abs(signed_height)
    stride = width * 4

    def pixel(x, y):
        row = y if signed_height < 0 else height - 1 - y
        b, g, r, _ = data[offset + row * stride + x * 4:offset + row * stride + x * 4 + 4]
        return r, g, b

    bounds = [(x, y) for y in range(height) for x in range(width)
              if max(pixel(x, y)) > 70]
    x0 = min(x for x, _ in bounds)
    x1 = max(x for x, _ in bounds) + 1
    y0 = min(y for _, y in bounds)
    y1 = max(y for _, y in bounds) + 1
    target_width = 260
    target_height = round((y1 - y0) * target_width / (x1 - x0))
    pixels = []
    for y in range(target_height):
        for x in range(target_width):
            r, g, b = pixel(x0 + (x * (x1 - x0)) // target_width,
                            y0 + (y * (y1 - y0)) // target_height)
            pixels.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    lines = ["#pragma once", "#include <stdint.h>", "",
             "// Source image colors preserved at the display's RGB565 depth.",
             f"constexpr uint16_t LOGO_WIDTH = {target_width};",
             f"constexpr uint16_t LOGO_HEIGHT = {target_height};",
             "const uint16_t LOGO_PIXELS[] PROGMEM = {"]
    for i in range(0, len(pixels), 16):
        lines.append("  " + ", ".join(f"0x{v:04X}" for v in pixels[i:i + 16]) + ",")
    lines.append("};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
    print(f"logo: {target_width}x{target_height}, {len(pixels) * 2} bytes")


if __name__ == "__main__":
    main()
