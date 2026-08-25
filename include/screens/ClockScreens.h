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

#ifndef SCREENS_CLOCKSCREENS_H
#define SCREENS_CLOCKSCREENS_H

#include <Arduino.h>
#include <array>

#include "screens/Screen.h"
#include "time/TimeService.h"

/**
 * @brief Which of the three clock layouts a TimeStyleScreen renders
 */
enum ClockStyle : uint8_t {
    STYLE_LARGE = 0,      // Hours and minutes only, filling the panel
    STYLE_COMPACT,        // Hours, minutes and seconds
    STYLE_WITH_WEATHER,   // Compact clock plus the current temperature
};

/**
 * @brief One of the three configurable clock themes
 */
class TimeStyleScreen : public Screen {
   public:
    explicit TimeStyleScreen(ClockStyle style, const char* screenName) : _style(style), _name(screenName) {}

    void enter() override;
    void tick() override;

    auto needsWeather() const -> bool override { return _style == STYLE_WITH_WEATHER; }
    auto name() const -> const char* override { return _name; }

   private:
    static constexpr uint8_t LARGE_FONT_SIZE = 6;
    static constexpr uint8_t MEDIUM_FONT_SIZE = 4;
    static constexpr uint8_t DATE_FONT_SIZE = 2;
    static constexpr int16_t ROW_LIFT = 12;
    static constexpr int16_t DATE_ROW_Y = 168;
    static constexpr int16_t EXTRA_ROW_Y = 196;

    void drawBitmap(const LocalTime& local);
    void drawSevenSegment(const LocalTime& local);
    void drawSecondaryRow(const LocalTime& local);

    ClockStyle _style;
    const char* _name;

    String _timeCache;
    String _minuteCache;
    String _secondCache;
    String _dateCache;
    String _extraCache;
    bool _lastColonVisible = true;
    std::array<uint8_t, 4> _lastDigits{};
};

#endif  // SCREENS_CLOCKSCREENS_H
