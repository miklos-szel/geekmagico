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
#include "display/IconBitmap.h"
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

// Icon basenames indexed by WeatherCondition, held in flash rather than DRAM:
// twelve string literals in .rodata would be DRAM on this part, which is the
// budget that actually binds here.
//
// NOLINTBEGIN(modernize-avoid-c-arrays): PROGMEM only applies to C arrays, and
// strncpy_P/pgm_read_ptr take PGM_P. A std::array here would move the strings
// back into DRAM, which is the whole reason the table exists.
const char WX_NAME_UNKNOWN[] PROGMEM = "unknown";
const char WX_NAME_CLEAR[] PROGMEM = "clear";
const char WX_NAME_PARTLY[] PROGMEM = "partly";
const char WX_NAME_CLOUDY[] PROGMEM = "cloudy";
const char WX_NAME_FOG[] PROGMEM = "fog";
const char WX_NAME_DRIZZLE[] PROGMEM = "drizzle";
const char WX_NAME_RAIN[] PROGMEM = "rain";
const char WX_NAME_SNOW[] PROGMEM = "snow";
const char WX_NAME_SLEET[] PROGMEM = "sleet";
const char WX_NAME_THUNDER[] PROGMEM = "thunder";
const char WX_NAME_CLEAR_NIGHT[] PROGMEM = "clear-n";
const char WX_NAME_PARTLY_NIGHT[] PROGMEM = "partly-n";

PGM_P const WX_NAMES[] PROGMEM = {
    WX_NAME_UNKNOWN, WX_NAME_CLEAR, WX_NAME_PARTLY, WX_NAME_CLOUDY, WX_NAME_FOG,
    WX_NAME_DRIZZLE, WX_NAME_RAIN,  WX_NAME_SNOW,   WX_NAME_SLEET,  WX_NAME_THUNDER,
};
// NOLINTEND(modernize-avoid-c-arrays)

constexpr uint8_t WX_NAME_COUNT = 10;
constexpr size_t WX_NAME_MAX = 12;
constexpr size_t WX_PATH_MAX = 24;

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

    // Every branch below fills centerX/centerY +- 3*unit. Holding that box is
    // what stops the icon appearing to jump when the condition changes -- the
    // slot is repainted, not re-laid-out, so an off-centre branch reads as a
    // misaligned icon rather than as a different drawing.

    // A cloud body, so the precipitation branches do not each re-derive one.
    // Drawn centred on `cloudY`, spanning +-3*unit across and 2.5*unit down.
    auto cloudAt = [&](int16_t cloudY) {
        gfx->fillCircle(centerX - unit, cloudY, unit * 2, CLOUD);
        gfx->fillCircle(centerX + ((unit * 3) / 2), cloudY, (unit * 3) / 2, CLOUD);
        gfx->fillRect(centerX - (unit * 3), cloudY, unit * 6, unit * 2, CLOUD);
    };

    switch (condition) {
        case WX_CLEAR:
            gfx->fillCircle(centerX, centerY, unit * 2, SUN);
            // Ray offsets are precomputed rather than derived with sinf/cosf:
            // pulling libm in for eight fixed points costs real DRAM here.
            // Scaled so the ray centres land at 2.5*unit and their radius
            // carries the glyph out to exactly 3*unit.
            for (const auto& ray : SUN_RAYS) {
                gfx->fillCircle(static_cast<int16_t>(centerX + ((ray.dx * unit) / 4)),
                                static_cast<int16_t>(centerY + ((ray.dy * unit) / 4)), unit / 2, SUN);
            }
            break;

        case WX_PARTLY_CLOUDY:
            gfx->fillCircle(centerX + unit, centerY - unit, unit * 2, SUN);
            gfx->fillCircle(centerX - unit, centerY + unit, unit * 2, CLOUD);
            gfx->fillCircle(centerX + unit + unit, centerY + unit, unit, CLOUD);
            gfx->fillRect(centerX - (unit * 3), centerY + unit, unit * 6, unit * 2, CLOUD);
            break;

        case WX_CLOUDY:
            // Sits half a unit high so the body, which hangs downward from the
            // circles, ends up centred rather than top-heavy.
            cloudAt(centerY - (unit / 2));
            gfx->fillRect(centerX - (unit * 3), centerY - (unit / 2), unit * 6, unit * 3, CLOUD);
            break;

        case WX_FOG:
            // Cloud lifted to make room for the bars, which carry the glyph
            // back down to 3*unit so it balances the taller branches.
            cloudAt(centerY - unit);
            gfx->fillRect(centerX - ((unit * 5) / 2), centerY + ((unit * 3) / 2), unit * 5, unit / 2, CLOUD);
            gfx->fillRect(centerX - (unit * 2), centerY + ((unit * 5) / 2), unit * 4, unit / 2, CLOUD);
            break;

        case WX_DRIZZLE:
        case WX_RAIN:
        case WX_SNOW:
        case WX_SLEET:
        case WX_THUNDER: {
            cloudAt(centerY - unit);

            if (condition == WX_THUNDER) {
                // Two mirrored triangles, so it reads as a bolt rather than as
                // the single lopsided wedge this used to draw.
                gfx->fillTriangle(centerX + unit, centerY + unit, centerX - unit, centerY + (unit * 2), centerX,
                                  centerY + (unit * 2), BOLT);
                gfx->fillTriangle(centerX + unit, centerY + (unit * 2), centerX - unit, centerY + (unit * 3), centerX,
                                  centerY + (unit * 2), BOLT);
            } else if (condition == WX_SNOW) {
                for (int16_t i = -1; i <= 1; ++i) {
                    gfx->fillCircle(centerX + (i * unit * 2), centerY + (unit * 2), unit / 2, SNOW);
                }
            } else {
                // Centre the group on centerX. fillRect anchors at its left
                // edge, so both the drop spacing and the drop width have to be
                // taken out of the starting offset -- missing that is what put
                // drizzle's two drops entirely left of centre.
                const int16_t drops = (condition == WX_DRIZZLE) ? 2 : 3;
                const int16_t spacing = unit * 2;
                const auto groupW = static_cast<int16_t>(((drops - 1) * spacing) + (unit / 2));
                const auto firstX = static_cast<int16_t>(centerX - (groupW / 2));

                for (int16_t i = 0; i < drops; ++i) {
                    gfx->fillRect(static_cast<int16_t>(firstX + (i * spacing)), centerY + unit, unit / 2, unit * 2,
                                  RAIN);
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

/**
 * @brief Whether a condition has night artwork, given a night reading
 *
 * Only clear and partly-cloudy differ after dark -- an overcast sky hides the
 * sun and the moon equally, so the other eight conditions share one image.
 *
 * This is the single definition of that rule on purpose. Callers cache the
 * icon they last drew and must key that cache on the same answer, or a device
 * would repaint an identical image at every sunrise and sunset.
 *
 * @param condition WeatherCondition value
 * @param night Whether it is night at the observed location
 *
 * @return true when the night variant should be used
 */
auto DrawUtils::weatherIconIsNight(uint8_t condition, bool night) -> bool {
    return night && (condition == WX_CLEAR || condition == WX_PARTLY_CLOUDY);
}

/**
 * @brief Draw a bundled weather icon from the filesystem
 *
 * Returns false when the icon set is not installed, which is the normal state
 * after a firmware-only update: the caller falls back to weatherGlyph(). That
 * makes this a soft dependency on littlefs.bin rather than a hard one.
 *
 * @param xPos Left edge of the icon slot
 * @param yPos Top edge of the icon slot
 * @param size Slot size in pixels, naming the icon set to draw from
 * @param condition WeatherCondition value
 * @param night Whether it is night at the observed location
 *
 * @return true when an icon was drawn
 */
auto DrawUtils::weatherIcon(int16_t xPos, int16_t yPos, int16_t size, uint8_t condition, bool night) -> bool {
    if (condition >= WX_NAME_COUNT) {
        return false;
    }

    std::array<char, WX_NAME_MAX> name{};

    if (DrawUtils::weatherIconIsNight(condition, night)) {
        strncpy_P(name.data(), (condition == WX_CLEAR) ? WX_NAME_CLEAR_NIGHT : WX_NAME_PARTLY_NIGHT, name.size() - 1);
    } else {
        strncpy_P(name.data(), reinterpret_cast<PGM_P>(pgm_read_ptr(&WX_NAMES[condition])), name.size() - 1);
    }

    // Longest real path is "/wx/80/partly-n.wxi" at 19 characters, so this
    // only fires if the table and the buffer drift apart.
    std::array<char, WX_PATH_MAX> path{};
    const int written = snprintf(path.data(), path.size(), "/wx/%d/%s.wxi", static_cast<int>(size), name.data());
    if (written <= 0 || static_cast<size_t>(written) >= path.size()) {
        return false;
    }

    return IconBitmap::draw(path.data(), xPos, yPos);
}
