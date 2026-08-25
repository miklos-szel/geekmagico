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

#ifndef SCREENS_SCREENMANAGER_H
#define SCREENS_SCREENMANAGER_H

#include <Arduino.h>
#include <cstdint>

/**
 * @brief Owns the display themes and the auto-switch rotation
 */
class ScreenManager {
   public:
    static void begin();
    static void loop();

    static void showTheme(uint8_t theme);
    static auto activeTheme() -> uint8_t;
    static auto activeThemeName() -> const char*;

    static void settingsChanged();
    static void suspend();
    static void resume();
    static auto isSuspended() -> bool;

    static auto themeName(uint8_t theme) -> const char*;
};

#endif  // SCREENS_SCREENMANAGER_H
