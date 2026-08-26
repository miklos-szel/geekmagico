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

#ifndef CONFIG_SETTINGS_H
#define CONFIG_SETTINGS_H

#include <ArduinoJson.h>
#include <cstdint>
#include <string>

/**
 * @brief Number of selectable display themes
 */
static constexpr uint8_t THEME_COUNT = 7;

/**
 * @brief Accepted range for a UTC offset, in minutes (UTC-12:00 .. UTC+14:00)
 *
 * Shared so the config loader and the API validate against the same bounds.
 */
static constexpr int16_t UTC_OFFSET_MIN_MINUTES = -720;
static constexpr int16_t UTC_OFFSET_MAX_MINUTES = 840;

/**
 * @brief Display theme identifiers, ordered as the Settings page lists them
 */
enum ThemeId : uint8_t {
    THEME_WEATHER_CLOCK = 0,
    THEME_WEATHER_FORECAST = 1,
    THEME_PHOTO_ALBUM = 2,
    THEME_TIME_STYLE_1 = 3,
    THEME_TIME_STYLE_2 = 4,
    THEME_TIME_STYLE_3 = 5,
    THEME_SIMPLE_WEATHER = 6,
};

/**
 * @brief Weather provider and presentation settings
 *
 * Values are stored in SI units; the unit fields only affect rendering.
 */
struct WeatherSettings {
    std::string api_key;       // OpenWeatherMap key; empty falls back to the keyless provider
    std::string forecast_key;  // Optional separate key for forecast lookups
    uint16_t interval_min = 20;
    std::string wind = "kmh";      // ms | kmh | mph
    std::string temp = "c";        // c | f
    std::string pressure = "hpa";  // hpa | inhg | mmhg
    std::string gif;               // 80x80 animation shown on the weather screen
};

/**
 * @brief Clock presentation settings
 *
 * Colors are RGB565 so they can go straight to the panel.
 */
struct TimeSettings {
    std::string tz_mode = "auto";  // auto (resolved online) | manual
    int16_t utc_offset_min = 0;
    int16_t auto_offset_min = 0;  // Last offset resolved in auto mode, kept across reboots
    uint16_t hour_color = 0xFFFF;
    uint16_t minute_color = 0xFD20;
    uint16_t second_color = 0xFFFF;
    bool format12h = false;
    std::string date_format = "DD/MM/YYYY";
    bool colon_blink = false;
    uint8_t font = 0;
};

/**
 * @brief Photo album settings
 */
struct PictureSettings {
    bool auto_display = true;
    bool shuffle = true;  // Show the album in a random order
    uint16_t interval_s = 30;
    std::string current;   // Pinned file when auto_display is off
};

/**
 * @brief Screen selection and backlight settings
 *
 * Night start/end are minutes past midnight so the wrap around midnight is
 * a plain integer comparison.
 */
struct DisplaySettings {
    uint8_t theme = THEME_WEATHER_CLOCK;
    bool auto_switch = false;
    uint16_t auto_switch_interval_s = 30;
    uint8_t auto_switch_mask = 0;  // One bit per ThemeId
    uint8_t brightness = 60;       // 0-100
    bool night_mode = true;
    uint16_t night_start = 22 * 60;
    uint16_t night_end = 7 * 60;
    uint8_t night_brightness = 15;  // 0-100
};

/**
 * @brief Everything the user can configure, minus secrets
 */
struct Settings {
    std::string city;
    WeatherSettings weather;
    TimeSettings time;
    PictureSettings pictures;
    DisplaySettings display;
};

void settingsFromJson(JsonObjectConst root, Settings& out);
void settingsToJson(const Settings& settings,  // NOLINT(readability-avoid-const-params-in-decls)
                    JsonObject root);

auto hexToRgb565(const char* hex, uint16_t fallback) -> uint16_t;
auto rgb565ToHex(uint16_t color) -> String;
auto hhmmToMinutes(const char* hhmm, uint16_t fallback) -> uint16_t;
auto minutesToHhmm(uint16_t minutes) -> String;

#endif  // CONFIG_SETTINGS_H
