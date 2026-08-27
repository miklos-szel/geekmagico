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

#ifndef SCREENS_DRAWUTILS_H
#define SCREENS_DRAWUTILS_H

#include <Arduino.h>
#include <cstdint>

/**
 * @brief Horizontal placement for cached text
 */
enum TextAlign : uint8_t {
    ALIGN_LEFT = 0,
    ALIGN_CENTER,
    ALIGN_RIGHT,
};

/**
 * @brief Shared drawing helpers for screens
 *
 * The cached* helpers only touch the panel when the value actually changed,
 * which is what keeps redraws flicker-free.
 */
class DrawUtils {
   public:
    static constexpr int16_t GLYPH_W = 6;
    static constexpr int16_t GLYPH_H = 8;

    static auto textWidth(const String& text, uint8_t size) -> int16_t;
    static auto textHeight(uint8_t size) -> int16_t;

    static void drawText(int16_t xPos, int16_t yPos, const String& text, uint8_t size, uint16_t foreground, uint16_t background,
                         TextAlign align);

    static auto cachedText(int16_t xPos, int16_t yPos, const String& text, String& cache, uint8_t size, uint16_t foreground,
                           uint16_t background, TextAlign align) -> bool;

    static void sevenSegDigit(int16_t xPos, int16_t yPos, int16_t width, int16_t height, int16_t thickness, uint8_t digit,
                              uint16_t foreground, uint16_t background);
    static void sevenSegColon(int16_t xPos, int16_t yPos, int16_t height, int16_t thickness, bool visible, uint16_t foreground,
                              uint16_t background);

    static void weatherGlyph(int16_t xPos, int16_t yPos, int16_t size, uint8_t condition, uint16_t background);

    static auto weatherIconIsNight(uint8_t condition, bool night) -> bool;
    static auto weatherIcon(int16_t xPos, int16_t yPos, int16_t size, uint8_t condition, bool night) -> bool;
};

#endif  // SCREENS_DRAWUTILS_H
