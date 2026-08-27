// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * GeekMagicO - a fork of GeekMagic Open Firmware
 * <https://github.com/Times-Z/GeekMagic-Open-Firmware>
 *
 * Copyright (C) 2026 Times-Z
 * Copyright (C) 2026 GeekMagicO contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef DISPLAY_ICONBITMAP_H
#define DISPLAY_ICONBITMAP_H

#include <Arduino.h>

/**
 * @brief Palette-indexed still images ('.wxi') drawn straight to the panel
 *
 * The format exists because the alternatives do not fit. AnimatedGIF wants
 * 24,172 bytes in one block on a device that idles near 20KB free, and JPEG
 * is the wrong tool for flat-shaded artwork. A '.wxi' is decoded by splitting
 * nibbles, so a whole icon costs one row of pixels in RAM and nothing on the
 * heap:
 *
 *   offset  size          field
 *   0       4             magic "WXI1"
 *   4       1             width       (<= MAX_DIMENSION)
 *   5       1             height      (<= MAX_DIMENSION)
 *   6       1             bpp         (always 4)
 *   7       1             paletteLen  (<= MAX_PALETTE)
 *   8       2*paletteLen  palette, little-endian RGB565
 *   ...     height*stride rows, top to bottom; high nibble is the left pixel
 *
 * Palette entry 0 is the background by convention, so drawing an icon also
 * clears the slot it lands in and callers need no separate fill.
 */
class IconBitmap {
   public:
    static constexpr uint8_t MAX_DIMENSION = 80;
    static constexpr uint8_t MAX_PALETTE = 16;
    static constexpr uint8_t BITS_PER_PIXEL = 4;

    static auto draw(const char* path, int16_t xPos, int16_t yPos) -> bool;
};

#endif  // DISPLAY_ICONBITMAP_H
