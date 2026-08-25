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

#include <Arduino.h>
#include <array>

#include "screens/DrawUtils.h"
#include "display/DisplayManager.h"
#include "weather/WeatherClient.h"

namespace {

// Segment layout, clockwise from the top bar then the two inner bars:
//   0 top, 1 top-right, 2 bottom-right, 3 bottom, 4 bottom-left,
//   5 top-left, 6 middle
constexpr std::array<uint8_t, 10> SEVEN_SEG_DIGITS = {
    0b0111111,  // 0
    0b0000110,  // 1
    0b1011011,  // 2
    0b1001111,  // 3
    0b1100110,  // 4
    0b1101101,  // 5
    0b1111101,  // 6
    0b0000111,  // 7
    0b1111111,  // 8
    0b1101111,  // 9
};

constexpr uint8_t SEG_TOP = 0;
constexpr uint8_t SEG_TOP_RIGHT = 1;
constexpr uint8_t SEG_BOTTOM_RIGHT = 2;
constexpr uint8_t SEG_BOTTOM = 3;
constexpr uint8_t SEG_BOTTOM_LEFT = 4;
constexpr uint8_t SEG_TOP_LEFT = 5;
constexpr uint8_t SEG_MIDDLE = 6;
constexpr uint8_t MAX_DIGIT = 9;

// Eight compass points, scaled by 10 so the maths stays in integers.
struct RayOffset {
    int16_t dx;
    int16_t dy;
};

constexpr std::array<RayOffset, 8> SUN_RAYS = {{
    {10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7},
}};

auto segmentOn(uint8_t digit, uint8_t segment) -> bool {
    if (digit > MAX_DIGIT) {
        return false;
    }

    return (SEVEN_SEG_DIGITS[digit] & (1U << segment)) != 0;
}

}  // namespace

/**
 * @brief Pixel width of a string in the built-in font
 *
 * @param text The string
 * @param size Font size multiplier
 *
 * @return Width in pixels
 */
auto DrawUtils::textWidth(const String& text, uint8_t size) -> int16_t {
    return static_cast<int16_t>(static_cast<int16_t>(text.length()) * GLYPH_W * size);
}

/**
 * @brief Pixel height of one text line
 *
 * @param size Font size multiplier
 *
 * @return Height in pixels
 */
auto DrawUtils::textHeight(uint8_t size) -> int16_t { return static_cast<int16_t>(GLYPH_H * size); }

/**
 * @brief Draw text, clearing exactly its own bounding box first
 *
 * @param xPos Anchor X, meaning depends on align
 * @param yPos Top Y
 * @param text The string to draw
 * @param size Font size multiplier
 * @param foreground Foreground colour
 * @param background Background colour
 * @param align Horizontal placement relative to xPos
 *
 * @return void
 */
void DrawUtils::drawText(int16_t xPos, int16_t yPos, const String& text, uint8_t size, uint16_t foreground, uint16_t background,
                         TextAlign align) {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    const int16_t width = DrawUtils::textWidth(text, size);

    int16_t drawX = xPos;
    if (align == ALIGN_CENTER) {
        drawX = static_cast<int16_t>(xPos - (width / 2));
    } else if (align == ALIGN_RIGHT) {
        drawX = static_cast<int16_t>(xPos - width);
    }

    gfx->setTextSize(size);
    gfx->setTextColor(foreground, background);
    gfx->setCursor(drawX, yPos);
    gfx->print(text);
}

/**
 * @brief Draw text only when it differs from the cached value
 *
 * Clears the previous string's box before drawing, so shrinking values do not
 * leave fragments behind.
 *
 * @param xPos Anchor X, meaning depends on align
 * @param yPos Top Y
 * @param text The string to draw
 * @param cache Previously drawn value, updated in place
 * @param size Font size multiplier
 * @param foreground Foreground colour
 * @param background Background colour
 * @param align Horizontal placement relative to xPos
 *
 * @return true when the panel was touched
 */
auto DrawUtils::cachedText(int16_t xPos, int16_t yPos, const String& text, String& cache, uint8_t size, uint16_t foreground,
                           uint16_t background, TextAlign align) -> bool {
    if (text == cache) {
        return false;
    }

    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return false;
    }

    if (cache.length() != 0) {
        const int16_t oldWidth = DrawUtils::textWidth(cache, size);
        int16_t oldX = xPos;
        if (align == ALIGN_CENTER) {
            oldX = static_cast<int16_t>(xPos - (oldWidth / 2));
        } else if (align == ALIGN_RIGHT) {
            oldX = static_cast<int16_t>(xPos - oldWidth);
        }

        gfx->fillRect(oldX, yPos, oldWidth, DrawUtils::textHeight(size), background);
    }

    DrawUtils::drawText(xPos, yPos, text, size, foreground, background, align);
    cache = text;

    return true;
}

/**
 * @brief Draw one seven-segment digit
 *
 * Gives the clock styles a look the built-in bitmap font cannot, without
 * spending flash on an embedded typeface.
 *
 * @param xPos Left edge
 * @param yPos Top edge
 * @param width Digit width
 * @param height Digit height
 * @param thickness Segment thickness
 * @param digit Value 0-9; anything else blanks the cell
 * @param foreground Segment colour
 * @param background Background colour
 *
 * @return void
 */
void DrawUtils::sevenSegDigit(int16_t xPos, int16_t yPos, int16_t width, int16_t height, int16_t thickness,
                              uint8_t digit, uint16_t foreground, uint16_t background) {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    gfx->fillRect(xPos, yPos, width, height, background);

    if (digit > MAX_DIGIT) {
        return;
    }

    const auto half = static_cast<int16_t>(height / 2);
    const auto inner = static_cast<int16_t>(width - (2 * thickness));
    const auto vertical = static_cast<int16_t>(half - thickness);

    if (segmentOn(digit, SEG_TOP)) {
        gfx->fillRect(static_cast<int16_t>(xPos + thickness), yPos, inner, thickness, foreground);
    }
    if (segmentOn(digit, SEG_MIDDLE)) {
        gfx->fillRect(static_cast<int16_t>(xPos + thickness), static_cast<int16_t>(yPos + half - (thickness / 2)), inner,
                      thickness, foreground);
    }
    if (segmentOn(digit, SEG_BOTTOM)) {
        gfx->fillRect(static_cast<int16_t>(xPos + thickness), static_cast<int16_t>(yPos + height - thickness), inner,
                      thickness, foreground);
    }
    if (segmentOn(digit, SEG_TOP_LEFT)) {
        gfx->fillRect(xPos, static_cast<int16_t>(yPos + thickness), thickness, vertical, foreground);
    }
    if (segmentOn(digit, SEG_TOP_RIGHT)) {
        gfx->fillRect(static_cast<int16_t>(xPos + width - thickness), static_cast<int16_t>(yPos + thickness), thickness,
                      vertical, foreground);
    }
    if (segmentOn(digit, SEG_BOTTOM_LEFT)) {
        gfx->fillRect(xPos, static_cast<int16_t>(yPos + half), thickness, vertical, foreground);
    }
    if (segmentOn(digit, SEG_BOTTOM_RIGHT)) {
        gfx->fillRect(static_cast<int16_t>(xPos + width - thickness), static_cast<int16_t>(yPos + half), thickness,
                      vertical, foreground);
    }
}

/**
 * @brief Draw the colon between seven-segment digit groups
 *
 * @param xPos Left edge
 * @param yPos Top edge of the digit row
 * @param height Digit height
 * @param thickness Dot size
 * @param visible Whether to draw or clear
 * @param foreground Dot colour
 * @param background Background colour
 *
 * @return void
 */
void DrawUtils::sevenSegColon(int16_t xPos, int16_t yPos, int16_t height, int16_t thickness, bool visible,
                              uint16_t foreground, uint16_t background) {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    const uint16_t paint = visible ? foreground : background;
    const auto upper = static_cast<int16_t>(yPos + (height / 3) - (thickness / 2));
    const auto lower = static_cast<int16_t>(yPos + ((2 * height) / 3) - (thickness / 2));

    gfx->fillRect(xPos, upper, thickness, thickness, paint);
    gfx->fillRect(xPos, lower, thickness, thickness, paint);
}

// Simple vector weather icons. Coordinates are proportional to `size` so the
// same routine serves both the large weather screen and the forecast columns.
// NOLINTBEGIN(readability-magic-numbers,bugprone-narrowing-conversions)
/**
 * @brief Draw a simple vector icon for a weather condition
 *
 * @param xPos Left edge
 * @param yPos Top edge
 * @param size Icon box size
 * @param condition A WeatherCondition value
 * @param background Background colour
 *
 * @return void
 */
void DrawUtils::weatherGlyph(int16_t xPos, int16_t yPos, int16_t size, uint8_t condition, uint16_t background) {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    gfx->fillRect(xPos, yPos, size, size, background);

    const int16_t centerX = xPos + (size / 2);
    const int16_t centerY = yPos + (size / 2);
    const int16_t unit = size / 8;

    constexpr uint16_t SUN = 0xFEA0;
    constexpr uint16_t CLOUD = 0xC618;
    constexpr uint16_t RAIN = 0x3DDF;
    constexpr uint16_t SNOW = 0xFFFF;
    constexpr uint16_t BOLT = 0xFFE0;

    switch (condition) {
        case WX_CLEAR:
            gfx->fillCircle(centerX, centerY, unit * 2, SUN);
            // Ray offsets are precomputed rather than derived with sinf/cosf:
            // pulling libm in for eight fixed points costs real DRAM here.
            for (const auto& ray : SUN_RAYS) {
                gfx->fillCircle(static_cast<int16_t>(centerX + ((ray.dx * unit * 3) / 10)),
                                static_cast<int16_t>(centerY + ((ray.dy * unit * 3) / 10)), unit / 2, SUN);
            }
            break;

        case WX_PARTLY_CLOUDY:
            gfx->fillCircle(centerX + unit, centerY - unit, unit * 2, SUN);
            gfx->fillCircle(centerX - unit, centerY + unit, unit * 2, CLOUD);
            gfx->fillCircle(centerX + unit + unit, centerY + unit, unit, CLOUD);
            gfx->fillRect(centerX - (unit * 3), centerY + unit, unit * 6, unit * 2, CLOUD);
            break;

        case WX_CLOUDY:
        case WX_FOG:
            gfx->fillCircle(centerX - unit, centerY, unit * 2, CLOUD);
            gfx->fillCircle(centerX + unit + (unit / 2), centerY, unit + (unit / 2), CLOUD);
            gfx->fillRect(centerX - (unit * 3), centerY, unit * 6, unit * 2, CLOUD);
            if (condition == WX_FOG) {
                gfx->fillRect(centerX - (unit * 3), centerY + (unit * 3), unit * 6, unit / 2, CLOUD);
            }
            break;

        case WX_DRIZZLE:
        case WX_RAIN:
        case WX_SNOW:
        case WX_SLEET:
        case WX_THUNDER: {
            gfx->fillCircle(centerX - unit, centerY - unit, unit * 2, CLOUD);
            gfx->fillCircle(centerX + unit + (unit / 2), centerY - unit, unit + (unit / 2), CLOUD);
            gfx->fillRect(centerX - (unit * 3), centerY - unit, unit * 6, unit * 2, CLOUD);

            if (condition == WX_THUNDER) {
                gfx->fillTriangle(centerX, centerY + unit, centerX + unit, centerY + (unit * 2), centerX - unit, centerY + (unit * 3), BOLT);
            } else if (condition == WX_SNOW) {
                for (int16_t i = -1; i <= 1; ++i) {
                    gfx->fillCircle(centerX + (i * unit * 2), centerY + (unit * 2), unit / 2, SNOW);
                }
            } else {
                const int16_t drops = (condition == WX_DRIZZLE) ? 2 : 3;
                for (int16_t i = 0; i < drops; ++i) {
                    const int16_t dropX = centerX + ((i - 1) * unit * 2);
                    gfx->fillRect(dropX, centerY + unit, unit / 2, unit * 2, RAIN);
                }
            }
            break;
        }

        case WX_UNKNOWN:
        default:
            gfx->drawCircle(centerX, centerY, unit * 3, CLOUD);
            break;
    }
}
// NOLINTEND(readability-magic-numbers,bugprone-narrowing-conversions)
