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
#include <Logger.h>
#include <array>

#include "screens/ScreenManager.h"
#include "screens/ClockScreens.h"
#include "screens/WeatherScreens.h"
#include "screens/PhotoAlbumScreen.h"
#include "config/ConfigManager.h"
#include "config/Settings.h"
#include "display/DisplayManager.h"

extern ConfigManager configManager;

namespace {

constexpr const char* TAG = "Screens";
constexpr uint32_t TICK_INTERVAL_MS = 200;
constexpr uint32_t MS_PER_SECOND = 1000;

// Statically allocated: seven screens on the heap would be needless churn on
// a device with roughly 39KB of it.
TimeStyleScreen timeStyle1(STYLE_LARGE, "Time Style 1");
TimeStyleScreen timeStyle2(STYLE_COMPACT, "Time Style 2");
TimeStyleScreen timeStyle3(STYLE_WITH_WEATHER, "Time Style 3");
WeatherClockScreen weatherClock;
WeatherForecastScreen weatherForecast;
SimpleWeatherClockScreen simpleWeatherClock;
PhotoAlbumScreen photoAlbum;

// Indexed by ThemeId.
const std::array<Screen*, THEME_COUNT> SCREENS = {
    &weatherClock,     // THEME_WEATHER_CLOCK
    &weatherForecast,  // THEME_WEATHER_FORECAST
    &photoAlbum,       // THEME_PHOTO_ALBUM
    &timeStyle1,       // THEME_TIME_STYLE_1
    &timeStyle2,       // THEME_TIME_STYLE_2
    &timeStyle3,       // THEME_TIME_STYLE_3
    &simpleWeatherClock,
};

uint8_t activeIndex = 0;
bool started = false;
bool suspended = false;
uint32_t lastTickMs = 0;
uint32_t enteredAtMs = 0;

/**
 * @brief Whether a theme is included in the auto-switch rotation
 */
auto inRotation(uint8_t theme) -> bool {
    return (configManager.settings.display.auto_switch_mask & (1U << theme)) != 0;
}

/**
 * @brief How many themes are in the rotation
 */
auto rotationSize() -> uint8_t {
    uint8_t total = 0;
    for (uint8_t theme = 0; theme < THEME_COUNT; ++theme) {
        if (inRotation(theme)) {
            total++;
        }
    }

    return total;
}

/**
 * @brief Next theme in the rotation after the given one
 */
auto nextInRotation(uint8_t from) -> uint8_t {
    for (uint8_t step = 1; step <= THEME_COUNT; ++step) {
        const auto candidate = static_cast<uint8_t>((from + step) % THEME_COUNT);
        if (inRotation(candidate)) {
            return candidate;
        }
    }

    return from;
}

}  // namespace

/**
 * @brief Human readable name of a theme
 *
 * @param theme The theme id
 *
 * @return The name, or "Unknown" when out of range
 */
auto ScreenManager::themeName(uint8_t theme) -> const char* {
    if (theme >= THEME_COUNT) {
        return "Unknown";
    }

    return SCREENS[theme]->name();
}

/**
 * @brief Show the configured theme and start ticking
 *
 * @return void
 */
void ScreenManager::begin() {
    started = true;
    suspended = false;

    ScreenManager::showTheme(configManager.settings.display.theme);
}

/**
 * @brief Switch to a theme immediately
 *
 * @param theme The theme id
 *
 * @return void
 */
void ScreenManager::showTheme(uint8_t theme) {
    const uint8_t target = (theme < THEME_COUNT) ? theme : 0;

    if (started && target == activeIndex && enteredAtMs != 0) {
        return;
    }

    SCREENS[activeIndex]->leave();

    activeIndex = target;
    enteredAtMs = millis();
    lastTickMs = 0;

    SCREENS[activeIndex]->enter();
    SCREENS[activeIndex]->tick();

    Logger::info((String("Theme: ") + SCREENS[activeIndex]->name()).c_str(), TAG);
}

/**
 * @brief Currently displayed theme
 *
 * @return The theme id
 */
auto ScreenManager::activeTheme() -> uint8_t { return activeIndex; }

/**
 * @brief Name of the currently displayed theme
 *
 * @return The name
 */
auto ScreenManager::activeThemeName() -> const char* { return SCREENS[activeIndex]->name(); }

/**
 * @brief Repaint the active theme after a settings change
 *
 * @return void
 */
void ScreenManager::settingsChanged() {
    if (!started || suspended) {
        return;
    }

    const uint8_t configured = configManager.settings.display.theme;

    if (configured != activeIndex) {
        ScreenManager::showTheme(configured);
        return;
    }

    // Same theme, but colours or units may have changed: force a full repaint.
    SCREENS[activeIndex]->leave();
    SCREENS[activeIndex]->enter();
    SCREENS[activeIndex]->tick();
}

/**
 * @brief Stop driving the panel
 *
 * Used while another subsystem owns the screen, such as an OTA progress bar.
 *
 * @return void
 */
void ScreenManager::suspend() {
    if (!started || suspended) {
        return;
    }

    SCREENS[activeIndex]->leave();
    suspended = true;
}

/**
 * @brief Resume driving the panel after a suspend
 *
 * @return void
 */
void ScreenManager::resume() {
    if (!started || !suspended) {
        return;
    }

    suspended = false;
    enteredAtMs = millis();

    SCREENS[activeIndex]->enter();
    SCREENS[activeIndex]->tick();
}

/**
 * @brief Whether the manager is currently suspended
 *
 * @return true when suspended
 */
auto ScreenManager::isSuspended() -> bool { return suspended; }

/**
 * @brief Tick the active screen and handle auto-switching
 *
 * @return void
 */
void ScreenManager::loop() {
    if (!started || suspended) {
        return;
    }

    const DisplaySettings& cfg = configManager.settings.display;

    // Auto-switch needs at least two themes selected, otherwise it would just
    // re-enter the same screen and flicker.
    if (cfg.auto_switch && rotationSize() >= 2) {
        const uint32_t dwellMs = static_cast<uint32_t>(cfg.auto_switch_interval_s) * MS_PER_SECOND;

        if ((millis() - enteredAtMs) >= dwellMs) {
            ScreenManager::showTheme(nextInRotation(activeIndex));
            return;
        }
    }

    if ((millis() - lastTickMs) < TICK_INTERVAL_MS) {
        return;
    }

    lastTickMs = millis();
    SCREENS[activeIndex]->tick();
}
