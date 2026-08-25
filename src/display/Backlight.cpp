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
#include <algorithm>

#include "display/Backlight.h"
#include "config/ConfigManager.h"

namespace {

constexpr uint8_t PERCENT_MAX = 100;
constexpr uint16_t PWM_RANGE = 255;
constexpr uint32_t PWM_FREQ_HZ = 1000;
constexpr uint16_t MINUTES_PER_DAY = 24 * 60;

uint8_t currentPercent = 0;
uint8_t dayPercent = 0;
uint8_t nightPercent = 0;
uint16_t nightStartMinute = 0;
uint16_t nightEndMinute = 0;
bool nightEnabled = false;
bool nightActive = false;
bool initialized = false;

/**
 * @brief Push a brightness percentage to the backlight pin
 *
 * @param percent Brightness in the range 0-100
 *
 * @return void
 */
void writeDuty(uint8_t percent) {
    const uint8_t clamped = std::min<uint8_t>(PERCENT_MAX, percent);
    auto duty = static_cast<uint16_t>((static_cast<uint32_t>(clamped) * PWM_RANGE) / PERCENT_MAX);

    // Active-low panel: full duty on the pin means the backlight is off.
    if (LCD_BACKLIGHT_ACTIVE_LOW) {
        duty = static_cast<uint16_t>(PWM_RANGE - duty);
    }

    analogWrite(static_cast<uint8_t>(LCD_BACKLIGHT_GPIO), static_cast<int>(duty));
}

}  // namespace

/**
 * @brief Initialize PWM on the backlight pin and apply a starting brightness
 *
 * @param brightnessPercent Initial brightness in the range 0-100
 *
 * @return void
 */
void Backlight::begin(uint8_t brightnessPercent) {
    pinMode(static_cast<uint8_t>(LCD_BACKLIGHT_GPIO), OUTPUT);
    analogWriteRange(PWM_RANGE);
    analogWriteFreq(PWM_FREQ_HZ);

    dayPercent = std::min<uint8_t>(PERCENT_MAX, brightnessPercent);
    currentPercent = dayPercent;
    initialized = true;

    writeDuty(currentPercent);

    Logger::info(("Backlight PWM ready at " + String(dayPercent) + "%").c_str(), "Backlight");
}

/**
 * @brief Apply a brightness immediately
 *
 * @param brightnessPercent Brightness in the range 0-100
 *
 * @return void
 */
void Backlight::setBrightness(uint8_t brightnessPercent) {
    const uint8_t clamped = std::min<uint8_t>(PERCENT_MAX, brightnessPercent);

    if (initialized && clamped == currentPercent) {
        return;
    }

    currentPercent = clamped;

    if (!initialized) {
        Backlight::begin(clamped);
        return;
    }

    writeDuty(clamped);
}

/**
 * @brief Currently applied brightness
 *
 * @return Brightness in the range 0-100
 */
auto Backlight::getBrightness() -> uint8_t { return currentPercent; }

/**
 * @brief Set the brightness used outside the night window
 *
 * @param brightnessPercent Brightness in the range 0-100
 *
 * @return void
 */
void Backlight::setDayBrightness(uint8_t brightnessPercent) {
    dayPercent = std::min<uint8_t>(PERCENT_MAX, brightnessPercent);

    if (!nightActive) {
        Backlight::setBrightness(dayPercent);
    }
}

/**
 * @brief Configure the night dimming schedule
 *
 * @param enabled Whether night dimming applies at all
 * @param startMinute Window start, minutes past midnight
 * @param endMinute Window end, minutes past midnight
 * @param nightBrightnessPercent Brightness to use inside the window
 *
 * @return void
 */
void Backlight::configureNightMode(bool enabled, uint16_t startMinute, uint16_t endMinute,
                                   uint8_t nightBrightnessPercent) {
    nightEnabled = enabled;
    nightStartMinute = startMinute % MINUTES_PER_DAY;
    nightEndMinute = endMinute % MINUTES_PER_DAY;
    nightPercent = std::min<uint8_t>(PERCENT_MAX, nightBrightnessPercent);

    if (!enabled && nightActive) {
        nightActive = false;
        Backlight::setBrightness(dayPercent);
    } else if (enabled && nightActive) {
        Backlight::setBrightness(nightPercent);
    }
}

/**
 * @brief Whether a given time falls inside a night window
 *
 * Handles windows that wrap past midnight, which is the common case: the
 * default 22:00-07:00 window has a start greater than its end.
 *
 * @param startMinute Window start, minutes past midnight
 * @param endMinute Window end, minutes past midnight
 * @param minutesOfDay Time to test, minutes past midnight
 *
 * @return true when the time is inside the window
 */
auto Backlight::isNightWindow(uint16_t startMinute, uint16_t endMinute, uint16_t minutesOfDay) -> bool {
    if (startMinute == endMinute) {
        return false;
    }

    if (startMinute < endMinute) {
        return minutesOfDay >= startMinute && minutesOfDay < endMinute;
    }

    // Wraps past midnight.
    return minutesOfDay >= startMinute || minutesOfDay < endMinute;
}

/**
 * @brief Whether night dimming is currently applied
 *
 * @return true when the night brightness is in effect
 */
auto Backlight::isNightActive() -> bool { return nightActive; }

/**
 * @brief Apply the night schedule for the current local time
 *
 * Cheap enough to call every loop; it only touches the pin on a transition.
 * A negative value means the local time is not known yet, in which case the
 * schedule is left alone rather than guessing.
 *
 * @param minutesOfDay Local time in minutes past midnight, or negative if unknown
 *
 * @return void
 */
void Backlight::applySchedule(int16_t minutesOfDay) {
    if (!nightEnabled || minutesOfDay < 0) {
        return;
    }

    const bool shouldDim =
        Backlight::isNightWindow(nightStartMinute, nightEndMinute, static_cast<uint16_t>(minutesOfDay));

    if (shouldDim == nightActive) {
        return;
    }

    nightActive = shouldDim;
    Backlight::setBrightness(shouldDim ? nightPercent : dayPercent);

    Logger::info(shouldDim ? "Night mode on" : "Night mode off", "Backlight");
}
