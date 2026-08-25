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

#include "screens/ClockScreens.h"
#include "screens/DrawUtils.h"
#include "display/DisplayManager.h"
#include "config/ConfigManager.h"
#include "time/TimeService.h"
#include "weather/WeatherClient.h"

extern ConfigManager configManager;

namespace {
constexpr uint16_t BG_COLOR = LCD_BLACK;
constexpr uint16_t MUTED = 0x8410;
constexpr uint8_t FONT_SEVEN_SEGMENT = 1;

// Character slots in the "HH:MM:SS" row.
constexpr int16_t CHARS_WITH_SECONDS = 8;
constexpr int16_t CHARS_WITHOUT_SECONDS = 5;
constexpr int16_t FIRST_COLON_OFFSET = 2;
constexpr int16_t MINUTE_CHAR_OFFSET = 3;
constexpr int16_t SECOND_COLON_OFFSET = 5;
constexpr int16_t SECOND_CHAR_OFFSET = 6;
constexpr int16_t SEG_DIGIT_COUNT = 4;
constexpr uint8_t DIGIT_BLANK = 0xFF;
constexpr int DECIMAL_BASE = 10;

// Layout for the seven-segment clock, sized to fill the 240px panel.
// NOLINTBEGIN(readability-magic-numbers)
constexpr int16_t SEG_DIGIT_W = 44;
constexpr int16_t SEG_DIGIT_H = 74;
constexpr int16_t SEG_THICK = 10;
constexpr int16_t SEG_GAP = 6;
constexpr int16_t SEG_COLON_W = 12;
constexpr int16_t SEG_ROW_Y = 74;
// NOLINTEND(readability-magic-numbers)
}  // namespace

/**
 * @brief Paint the static parts of a clock screen
 *
 * @return void
 */
void TimeStyleScreen::enter() {
    DisplayManager::getGfx()->fillScreen(BG_COLOR);

    _timeCache = "";
    _dateCache = "";
    _extraCache = "";
    _lastColonVisible = true;
    _colonPainted = false;
    _lastDigits.fill(DIGIT_BLANK);
}

/**
 * @brief Repaint whatever changed since the last tick
 *
 * @return void
 */
void TimeStyleScreen::tick() {
    const LocalTime local = TimeService::now();
    const TimeSettings& cfg = configManager.settings.time;

    if (cfg.font == FONT_SEVEN_SEGMENT && _style != STYLE_COMPACT) {
        drawSevenSegment(local);
    } else {
        drawBitmap(local);
    }

    drawSecondaryRow(local);
}

/**
 * @brief Render the time using the built-in font
 *
 * Hours, minutes and seconds are drawn separately so each can take its own
 * configured colour, which is what the original firmware's colour pickers do.
 *
 * @param local The current local time
 *
 * @return void
 */
void TimeStyleScreen::drawBitmap(const LocalTime& local) {
    const TimeSettings& cfg = configManager.settings.time;
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    const bool showSeconds = (_style != STYLE_LARGE);
    const uint8_t size = (_style == STYLE_LARGE) ? LARGE_FONT_SIZE : MEDIUM_FONT_SIZE;

    const int hour = TimeService::displayHour(local, cfg.format12h);
    const bool colonOn = TimeService::colonVisible(local, cfg.colon_blink);

    std::array<char, 4> hourBuf{};
    std::array<char, 4> minuteBuf{};
    std::array<char, 4> secondBuf{};
    snprintf(hourBuf.data(), hourBuf.size(), "%02d", local.valid ? hour : 0);
    snprintf(minuteBuf.data(), minuteBuf.size(), "%02d", local.valid ? local.minute : 0);
    snprintf(secondBuf.data(), secondBuf.size(), "%02d", local.valid ? local.second : 0);

    const auto glyph = static_cast<int16_t>(DrawUtils::GLYPH_W * size);
    const int16_t totalChars = showSeconds ? CHARS_WITH_SECONDS : CHARS_WITHOUT_SECONDS;
    const auto totalWidth = static_cast<int16_t>(totalChars * glyph);
    const auto startX = static_cast<int16_t>((gfx->width() - totalWidth) / 2);
    const auto rowY = static_cast<int16_t>((gfx->height() / 2) - (DrawUtils::textHeight(size) / 2) - ROW_LIFT);

    String hourStr(hourBuf.data());
    String minuteStr(minuteBuf.data());
    String secondStr(secondBuf.data());

    DrawUtils::cachedText(startX, rowY, hourStr, _timeCache, size, cfg.hour_color, BG_COLOR, ALIGN_LEFT);
    DrawUtils::cachedText(static_cast<int16_t>(startX + (MINUTE_CHAR_OFFSET * glyph)), rowY, minuteStr, _minuteCache, size,
                          cfg.minute_color, BG_COLOR, ALIGN_LEFT);

    if (showSeconds) {
        DrawUtils::cachedText(static_cast<int16_t>(startX + (SECOND_CHAR_OFFSET * glyph)), rowY, secondStr, _secondCache, size,
                              cfg.second_color, BG_COLOR, ALIGN_LEFT);
    }

    // The colon is repainted only when its state flips, so blinking costs one
    // small rectangle per second rather than a full redraw.
    if (colonOn != _lastColonVisible || _timeCache.length() == 0) {
        _lastColonVisible = colonOn;

        gfx->setTextSize(size);
        gfx->setTextColor(colonOn ? cfg.minute_color : BG_COLOR, BG_COLOR);
        gfx->setCursor(static_cast<int16_t>(startX + (FIRST_COLON_OFFSET * glyph)), rowY);
        gfx->print(':');

        if (showSeconds) {
            gfx->setCursor(static_cast<int16_t>(startX + (SECOND_COLON_OFFSET * glyph)), rowY);
            gfx->print(':');
        }
    }
}

/**
 * @brief Render the time as seven-segment digits
 *
 * @param local The current local time
 *
 * @return void
 */
void TimeStyleScreen::drawSevenSegment(const LocalTime& local) {
    const TimeSettings& cfg = configManager.settings.time;
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    const int hour = TimeService::displayHour(local, cfg.format12h);
    const std::array<uint8_t, 4> digits = {
        static_cast<uint8_t>(local.valid ? (hour / DECIMAL_BASE) : DIGIT_BLANK),
        static_cast<uint8_t>(local.valid ? (hour % DECIMAL_BASE) : DIGIT_BLANK),
        static_cast<uint8_t>(local.valid ? (local.minute / DECIMAL_BASE) : DIGIT_BLANK),
        static_cast<uint8_t>(local.valid ? (local.minute % DECIMAL_BASE) : DIGIT_BLANK),
    };

    const auto groupWidth =
        static_cast<int16_t>((SEG_DIGIT_COUNT * SEG_DIGIT_W) + (2 * SEG_GAP) + SEG_COLON_W + (2 * SEG_GAP));
    auto xPos = static_cast<int16_t>((gfx->width() - groupWidth) / 2);

    for (size_t i = 0; i < digits.size(); ++i) {
        if (i == static_cast<size_t>(FIRST_COLON_OFFSET)) {
            xPos = static_cast<int16_t>(xPos + SEG_COLON_W + (2 * SEG_GAP));
        }

        if (digits[i] != _lastDigits[i]) {
            const uint16_t color = (i < static_cast<size_t>(FIRST_COLON_OFFSET)) ? cfg.hour_color : cfg.minute_color;
            DrawUtils::sevenSegDigit(xPos, SEG_ROW_Y, SEG_DIGIT_W, SEG_DIGIT_H, SEG_THICK, digits[i], color, BG_COLOR);
            _lastDigits[i] = digits[i];
        }

        xPos = static_cast<int16_t>(xPos + SEG_DIGIT_W + SEG_GAP);
    }

    const bool colonOn = TimeService::colonVisible(local, cfg.colon_blink);
    if (colonOn != _lastColonVisible || !_colonPainted) {
        _lastColonVisible = colonOn;
        _colonPainted = true;

        const auto colonX =
            static_cast<int16_t>(((gfx->width() - groupWidth) / 2) + (FIRST_COLON_OFFSET * SEG_DIGIT_W) + (2 * SEG_GAP));
        DrawUtils::sevenSegColon(colonX, SEG_ROW_Y, SEG_DIGIT_H, SEG_THICK, colonOn, cfg.minute_color, BG_COLOR);
    }
}

/**
 * @brief Draw the date, and on the weather-aware style the temperature
 *
 * @param local The current local time
 *
 * @return void
 */
void TimeStyleScreen::drawSecondaryRow(const LocalTime& local) {
    const TimeSettings& cfg = configManager.settings.time;
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return;
    }

    const auto centerX = static_cast<int16_t>(gfx->width() / 2);

    DrawUtils::cachedText(centerX, DATE_ROW_Y, TimeService::formatDate(local, cfg.date_format.c_str()), _dateCache,
                          DATE_FONT_SIZE, MUTED, BG_COLOR, ALIGN_CENTER);

    String extra;
    if (_style == STYLE_WITH_WEATHER) {
        const WeatherNow& weather = WeatherClient::current();
        if (weather.valid) {
            extra = String(WeatherClient::toDisplayTemp(weather.tempC), 1) + String(WeatherClient::temperatureUnit()) +
                    "  " + WeatherClient::conditionLabel(weather.condition);
        } else {
            extra = "--";
        }
    } else if (cfg.format12h && local.valid) {
        extra = TimeService::isPm(local) ? "PM" : "AM";
    }

    DrawUtils::cachedText(centerX, EXTRA_ROW_Y, extra, _extraCache, DATE_FONT_SIZE, MUTED, BG_COLOR, ALIGN_CENTER);
}
