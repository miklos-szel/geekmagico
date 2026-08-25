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
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "config/Settings.h"

namespace {

constexpr uint8_t HEX_BASE = 16;
constexpr uint8_t RGB565_RED_SHIFT = 8;
constexpr uint8_t RGB565_GREEN_SHIFT = 3;
constexpr uint8_t RGB565_BLUE_SHIFT = 3;
constexpr uint16_t RGB565_RED_MASK = 0xF800;
constexpr uint16_t RGB565_GREEN_MASK = 0x07E0;
constexpr uint16_t RGB565_BLUE_MASK = 0x001F;
constexpr uint32_t RGB888_MAX = 0xFFFFFF;
constexpr uint8_t HEX_STRING_LEN = 6;
constexpr size_t COLOR_BUF_SIZE = 8;
constexpr size_t TIME_BUF_SIZE = 8;
constexpr uint16_t MINUTES_PER_DAY = 24 * 60;
constexpr uint16_t MINUTES_PER_HOUR = 60;
constexpr uint8_t PERCENT_MAX = 100;
constexpr uint8_t RGB565_RED_KEEP = 0xF8;
constexpr uint8_t RGB565_GREEN_KEEP = 0xFC;
constexpr int HOUR_MAX = 23;
constexpr int MINUTE_MAX = 59;
constexpr uint16_t WEATHER_INTERVAL_MIN = 1;
constexpr uint16_t WEATHER_INTERVAL_MAX = 1440;
constexpr int16_t UTC_OFFSET_MIN_MINUTES = -720;
constexpr int16_t UTC_OFFSET_MAX_MINUTES = 840;
constexpr uint8_t FONT_MAX = 3;
constexpr uint16_t PICTURE_INTERVAL_MIN = 1;
constexpr uint16_t PICTURE_INTERVAL_MAX = 3600;
constexpr uint16_t SWITCH_INTERVAL_MIN = 3;
constexpr uint16_t SWITCH_INTERVAL_MAX = 3600;
constexpr uint8_t THEME_MASK_ALL = 0x7F;

// Wrapping the subscript in one place keeps the clang-tidy suppression from
// spreading across every field read below.
auto member(JsonObjectConst obj, const char* key) -> JsonVariantConst {
    return obj[key];  // NOLINT(readability-misplaced-array-index)
}

void readBool(JsonObjectConst obj, const char* key, bool& out) {
    JsonVariantConst value = member(obj, key);
    if (value.is<bool>()) {
        out = value.as<bool>();
    }
}

void readString(JsonObjectConst obj, const char* key, std::string& out) {
    JsonVariantConst value = member(obj, key);
    if (value.is<const char*>()) {
        const char* str = value.as<const char*>();
        if (str != nullptr) {
            out = str;
        }
    }
}

template <typename T>
void readNumber(JsonObjectConst obj, const char* key, T& out, T minValue, T maxValue) {
    JsonVariantConst value = member(obj, key);
    if (value.is<T>()) {
        out = std::min(maxValue, std::max(minValue, value.as<T>()));
    }
}

void readColor(JsonObjectConst obj, const char* key, uint16_t& out) {
    JsonVariantConst value = member(obj, key);
    if (value.is<const char*>()) {
        out = hexToRgb565(value.as<const char*>(), out);
    } else if (value.is<uint16_t>()) {
        out = value.as<uint16_t>();
    }
}

void readClock(JsonObjectConst obj, const char* key, uint16_t& out) {
    JsonVariantConst value = member(obj, key);
    if (value.is<const char*>()) {
        out = hhmmToMinutes(value.as<const char*>(), out);
    } else if (value.is<uint16_t>()) {
        out = std::min<uint16_t>(MINUTES_PER_DAY - 1, value.as<uint16_t>());
    }
}

// Writes go through the same single suppression point.
template <typename T>
void writeMember(JsonObject obj, const char* key, T value) {
    obj[key] = value;  // NOLINT(readability-misplaced-array-index)
}

auto childObject(JsonObject parent, const char* key) -> JsonObject {
    return parent[key].to<JsonObject>();  // NOLINT(readability-misplaced-array-index)
}

auto childObjectConst(JsonObjectConst parent, const char* key) -> JsonObjectConst {
    return member(parent, key).as<JsonObjectConst>();
}

}  // namespace

/**
 * @brief Convert a "#RRGGBB" string to RGB565
 *
 * @param hex The color string, with or without a leading '#'
 * @param fallback Value returned when the string cannot be parsed
 *
 * @return The color as RGB565
 */
auto hexToRgb565(const char* hex, uint16_t fallback) -> uint16_t {
    if (hex == nullptr) {
        return fallback;
    }

    const char* digits = (hex[0] == '#') ? hex + 1 : hex;
    if (strlen(digits) != HEX_STRING_LEN) {
        return fallback;
    }

    char* end = nullptr;
    const uint32_t rgb = strtoul(digits, &end, HEX_BASE);
    if (end == nullptr || *end != '\0' || rgb > RGB888_MAX) {
        return fallback;
    }

    const auto red = static_cast<uint16_t>((rgb >> 16U) & 0xFFU);
    const auto green = static_cast<uint16_t>((rgb >> 8U) & 0xFFU);
    const auto blue = static_cast<uint16_t>(rgb & 0xFFU);

    return static_cast<uint16_t>(((red & RGB565_RED_KEEP) << RGB565_RED_SHIFT) | ((green & RGB565_GREEN_KEEP) << RGB565_GREEN_SHIFT) |
                                 (blue >> RGB565_BLUE_SHIFT));
}

/**
 * @brief Convert an RGB565 color to a "#RRGGBB" string
 *
 * @param color The color as RGB565
 *
 * @return The color string
 */
auto rgb565ToHex(uint16_t color) -> String {
    const auto red = static_cast<uint8_t>(((color & RGB565_RED_MASK) >> 11U) * 255U / 31U);
    const auto green = static_cast<uint8_t>(((color & RGB565_GREEN_MASK) >> 5U) * 255U / 63U);
    const auto blue = static_cast<uint8_t>((color & RGB565_BLUE_MASK) * 255U / 31U);

    std::array<char, COLOR_BUF_SIZE> buf{};
    snprintf(buf.data(), buf.size(), "#%02X%02X%02X", red, green, blue);

    return {buf.data()};
}

/**
 * @brief Convert a "HH:MM" string to minutes past midnight
 *
 * @param hhmm The time string
 * @param fallback Value returned when the string cannot be parsed
 *
 * @return Minutes past midnight
 */
auto hhmmToMinutes(const char* hhmm, uint16_t fallback) -> uint16_t {
    if (hhmm == nullptr) {
        return fallback;
    }

    int hours = 0;
    int minutes = 0;
    if (sscanf(hhmm, "%d:%d", &hours, &minutes) != 2) {
        return fallback;
    }

    if (hours < 0 || hours > HOUR_MAX || minutes < 0 || minutes > MINUTE_MAX) {
        return fallback;
    }

    return static_cast<uint16_t>((hours * MINUTES_PER_HOUR) + minutes);
}

/**
 * @brief Convert minutes past midnight to a "HH:MM" string
 *
 * @param minutes Minutes past midnight
 *
 * @return The time string
 */
auto minutesToHhmm(uint16_t minutes) -> String {
    const uint16_t wrapped = minutes % MINUTES_PER_DAY;

    std::array<char, TIME_BUF_SIZE> buf{};
    snprintf(buf.data(), buf.size(), "%02u:%02u", wrapped / MINUTES_PER_HOUR, wrapped % MINUTES_PER_HOUR);

    return {buf.data()};
}

/**
 * @brief Populate settings from a parsed config document
 *
 * Missing keys and sections leave the corresponding defaults untouched, so an
 * older config.json upgrades in place.
 *
 * @param root The config root object
 * @param out The settings to fill
 *
 * @return void
 */
void settingsFromJson(JsonObjectConst root, Settings& out) {
    readString(root, "city", out.city);

    JsonObjectConst weather = childObjectConst(root, "weather");
    if (!weather.isNull()) {
        readString(weather, "api_key", out.weather.api_key);
        readString(weather, "forecast_key", out.weather.forecast_key);
        readNumber<uint16_t>(weather, "interval_min", out.weather.interval_min, WEATHER_INTERVAL_MIN,
                             WEATHER_INTERVAL_MAX);
        readString(weather, "wind", out.weather.wind);
        readString(weather, "temp", out.weather.temp);
        readString(weather, "pressure", out.weather.pressure);
        readString(weather, "gif", out.weather.gif);
    }

    JsonObjectConst timeCfg = childObjectConst(root, "time");
    if (!timeCfg.isNull()) {
        readString(timeCfg, "tz_mode", out.time.tz_mode);
        readNumber<int16_t>(timeCfg, "utc_offset_min", out.time.utc_offset_min, UTC_OFFSET_MIN_MINUTES,
                            UTC_OFFSET_MAX_MINUTES);
        readColor(timeCfg, "hour_color", out.time.hour_color);
        readColor(timeCfg, "minute_color", out.time.minute_color);
        readColor(timeCfg, "second_color", out.time.second_color);
        readBool(timeCfg, "format12h", out.time.format12h);
        readString(timeCfg, "date_format", out.time.date_format);
        readBool(timeCfg, "colon_blink", out.time.colon_blink);
        readNumber<uint8_t>(timeCfg, "font", out.time.font, 0, FONT_MAX);
    }

    JsonObjectConst pictures = childObjectConst(root, "pictures");
    if (!pictures.isNull()) {
        readBool(pictures, "auto_display", out.pictures.auto_display);
        readBool(pictures, "shuffle", out.pictures.shuffle);
        readNumber<uint16_t>(pictures, "interval_s", out.pictures.interval_s, PICTURE_INTERVAL_MIN,
                             PICTURE_INTERVAL_MAX);
        readString(pictures, "current", out.pictures.current);
    }

    JsonObjectConst display = childObjectConst(root, "display");
    if (!display.isNull()) {
        readNumber<uint8_t>(display, "theme", out.display.theme, 0, THEME_COUNT - 1);
        readBool(display, "auto_switch", out.display.auto_switch);
        readNumber<uint16_t>(display, "auto_switch_interval_s", out.display.auto_switch_interval_s,
                             SWITCH_INTERVAL_MIN, SWITCH_INTERVAL_MAX);
        readNumber<uint8_t>(display, "auto_switch_mask", out.display.auto_switch_mask, 0, THEME_MASK_ALL);
        readNumber<uint8_t>(display, "brightness", out.display.brightness, 0, PERCENT_MAX);
        readBool(display, "night_mode", out.display.night_mode);
        readClock(display, "night_start", out.display.night_start);
        readClock(display, "night_end", out.display.night_end);
        readNumber<uint8_t>(display, "night_brightness", out.display.night_brightness, 0, PERCENT_MAX);
    }
}

/**
 * @brief Serialize settings into a config document
 *
 * @param settings The settings to write
 * @param root The config root object
 *
 * @return void
 */
void settingsToJson(const Settings& settings, JsonObject root) {
    writeMember(root, "city", settings.city.c_str());

    JsonObject weather = childObject(root, "weather");
    writeMember(weather, "api_key", settings.weather.api_key.c_str());
    writeMember(weather, "forecast_key", settings.weather.forecast_key.c_str());
    writeMember(weather, "interval_min", settings.weather.interval_min);
    writeMember(weather, "wind", settings.weather.wind.c_str());
    writeMember(weather, "temp", settings.weather.temp.c_str());
    writeMember(weather, "pressure", settings.weather.pressure.c_str());
    writeMember(weather, "gif", settings.weather.gif.c_str());

    JsonObject timeCfg = childObject(root, "time");
    writeMember(timeCfg, "tz_mode", settings.time.tz_mode.c_str());
    writeMember(timeCfg, "utc_offset_min", settings.time.utc_offset_min);
    writeMember(timeCfg, "hour_color", settings.time.hour_color);
    writeMember(timeCfg, "minute_color", settings.time.minute_color);
    writeMember(timeCfg, "second_color", settings.time.second_color);
    writeMember(timeCfg, "format12h", settings.time.format12h);
    writeMember(timeCfg, "date_format", settings.time.date_format.c_str());
    writeMember(timeCfg, "colon_blink", settings.time.colon_blink);
    writeMember(timeCfg, "font", settings.time.font);

    JsonObject pictures = childObject(root, "pictures");
    writeMember(pictures, "auto_display", settings.pictures.auto_display);
    writeMember(pictures, "shuffle", settings.pictures.shuffle);
    writeMember(pictures, "interval_s", settings.pictures.interval_s);
    writeMember(pictures, "current", settings.pictures.current.c_str());

    JsonObject display = childObject(root, "display");
    writeMember(display, "theme", settings.display.theme);
    writeMember(display, "auto_switch", settings.display.auto_switch);
    writeMember(display, "auto_switch_interval_s", settings.display.auto_switch_interval_s);
    writeMember(display, "auto_switch_mask", settings.display.auto_switch_mask);
    writeMember(display, "brightness", settings.display.brightness);
    writeMember(display, "night_mode", settings.display.night_mode);
    writeMember(display, "night_start", settings.display.night_start);
    writeMember(display, "night_end", settings.display.night_end);
    writeMember(display, "night_brightness", settings.display.night_brightness);
}
