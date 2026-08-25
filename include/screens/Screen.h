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

#ifndef SCREENS_SCREEN_H
#define SCREENS_SCREEN_H

#include <Arduino.h>

/**
 * @brief One display theme
 *
 * enter() paints the static chrome once; tick() is called every loop and must
 * repaint only what changed. Full-screen redraws on a 40MHz SPI bus flicker
 * and starve the main loop, so screens cache their last rendered values.
 */
class Screen {
   public:
    Screen() = default;
    Screen(const Screen&) = delete;
    Screen(Screen&&) = delete;
    auto operator=(const Screen&) -> Screen& = delete;
    auto operator=(Screen&&) -> Screen& = delete;
    virtual ~Screen() = default;

    virtual void enter() = 0;
    virtual void tick() = 0;
    virtual void leave() {}

    /**
     * @brief Whether this screen needs current conditions
     */
    virtual auto needsWeather() const -> bool { return false; }

    /**
     * @brief Whether this screen needs the multi-day forecast
     *
     * The forecast is a second HTTP request, so it is only fetched for screens
     * that actually show it.
     */
    virtual auto needsForecast() const -> bool { return false; }

    virtual auto name() const -> const char* = 0;
};

#endif  // SCREENS_SCREEN_H
