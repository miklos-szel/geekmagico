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

#ifndef DISPLAY_BACKLIGHT_H
#define DISPLAY_BACKLIGHT_H

#include <Arduino.h>
#include <cstdint>

/**
 * @brief PWM control of the LCD backlight, with an optional night schedule
 *
 * The panel's backlight pin is active-low on this hardware, so duty cycles are
 * inverted before they reach the pin.
 */
class Backlight {
   public:
    static void begin(uint8_t brightnessPercent);
    static void setBrightness(uint8_t brightnessPercent);
    static auto getBrightness() -> uint8_t;

    static void configureNightMode(bool enabled, uint16_t startMinute, uint16_t endMinute,
                                   uint8_t nightBrightnessPercent);
    static void setDayBrightness(uint8_t brightnessPercent);
    static void applySchedule(int16_t minutesOfDay);

    static auto isNightWindow(uint16_t startMinute, uint16_t endMinute, uint16_t minutesOfDay) -> bool;
    static auto isNightActive() -> bool;
};

#endif  // DISPLAY_BACKLIGHT_H
