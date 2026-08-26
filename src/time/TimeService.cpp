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
#include <cstdio>
#include <cstring>

#include "time/TimeService.h"
#include "config/ConfigManager.h"

extern ConfigManager configManager;

namespace {

constexpr time_t REASONABLE_EPOCH = 1600000000UL;
constexpr int TM_YEAR_BASE = 1900;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int MINUTES_PER_HOUR = 60;
constexpr int HOURS_PER_HALF_DAY = 12;
constexpr size_t TIME_BUF_SIZE = 16;
constexpr size_t DATE_BUF_SIZE = 16;

// One slot per automatic source; TZ_OFFSET_MANUAL is read from config instead.
std::array<int32_t, TZ_OFFSET_SOURCE_COUNT> sourceSeconds{};
std::array<bool, TZ_OFFSET_SOURCE_COUNT> sourceKnown{};

/**
 * @brief Highest-precedence source that currently has an offset
 *
 * @return The winning source, TZ_OFFSET_MANUAL when nothing was resolved
 */
auto winningSource() -> TzOffsetSource {
    const TimeSettings& cfg = configManager.settings.time;

    if (cfg.tz_mode == "manual") {
        return TZ_OFFSET_MANUAL;
    }

    for (uint8_t src = TZ_OFFSET_SOURCE_COUNT - 1; src > TZ_OFFSET_MANUAL; --src) {
        if (sourceKnown.at(src)) {
            return static_cast<TzOffsetSource>(src);
        }
    }

    // Nothing resolved yet: fall back to whatever manual offset is stored
    // rather than silently showing UTC.
    return TZ_OFFSET_MANUAL;
}

/**
 * @brief Resolve the offset from UTC to apply, in seconds
 *
 * @return Offset in seconds
 */
auto resolveOffsetSeconds() -> int32_t {
    const TzOffsetSource src = winningSource();

    if (src == TZ_OFFSET_MANUAL) {
        return static_cast<int32_t>(configManager.settings.time.utc_offset_min) * SECONDS_PER_MINUTE;
    }

    return sourceSeconds.at(src);
}

/**
 * @brief Keep a resolved offset across reboots
 *
 * Writing config.json costs a flash erase, so this only persists when the
 * value actually moved - a steady device writes once, and again at each DST
 * transition.
 *
 * @param offsetMinutes Offset from UTC in minutes
 *
 * @return void
 */
void cacheOffsetMinutes(int16_t offsetMinutes) {
    TimeSettings& cfg = configManager.settings.time;

    sourceSeconds.at(TZ_OFFSET_CACHED) = static_cast<int32_t>(offsetMinutes) * SECONDS_PER_MINUTE;
    sourceKnown.at(TZ_OFFSET_CACHED) = true;

    if (cfg.auto_offset_min == offsetMinutes) {
        return;
    }

    cfg.auto_offset_min = offsetMinutes;
    configManager.save();
}

}  // namespace

/**
 * @brief Record an offset resolved from one of the automatic sources
 *
 * @param source Which source reported it
 * @param offsetSeconds Offset from UTC in seconds
 *
 * @return void
 */
void TimeService::setOffsetSeconds(TzOffsetSource source, int32_t offsetSeconds) {
    if (source == TZ_OFFSET_MANUAL) {
        return;
    }

    sourceSeconds.at(source) = offsetSeconds;
    sourceKnown.at(source) = true;

    if (source != TZ_OFFSET_CACHED) {
        cacheOffsetMinutes(static_cast<int16_t>(offsetSeconds / SECONDS_PER_MINUTE));
    }
}

/**
 * @brief Forget the offset from one source
 *
 * Called when a lookup succeeds but carries no offset - the keyless weather
 * feed never reports one - so a stale value from an earlier fetch does not
 * keep being applied. Lower-precedence sources stay in place, which is what
 * keeps a good IP lookup alive across a keyless weather refresh.
 *
 * @param source Which source to forget
 *
 * @return void
 */
void TimeService::clearOffsetSource(TzOffsetSource source) {
    if (source == TZ_OFFSET_MANUAL) {
        return;
    }

    sourceKnown.at(source) = false;
}

/**
 * @brief Seed the cached slot from config at boot
 *
 * Without this the clock shows UTC from boot until the first successful
 * lookup, which on a slow join is several seconds of visibly wrong time.
 *
 * @return void
 */
void TimeService::restoreCachedOffset() {
    const int16_t cached = configManager.settings.time.auto_offset_min;

    if (cached == 0) {
        return;
    }

    sourceSeconds.at(TZ_OFFSET_CACHED) = static_cast<int32_t>(cached) * SECONDS_PER_MINUTE;
    sourceKnown.at(TZ_OFFSET_CACHED) = true;
}

/**
 * @brief The offset currently being applied, in minutes
 *
 * @return Offset in minutes
 */
auto TimeService::offsetMinutes() -> int16_t {
    return static_cast<int16_t>(resolveOffsetSeconds() / SECONDS_PER_MINUTE);
}

/**
 * @brief Which source the applied offset came from
 *
 * @return The winning source
 */
auto TimeService::activeSource() -> TzOffsetSource { return winningSource(); }

/**
 * @brief Stable identifier for a source, for the API and the web UI
 *
 * @param source The source
 *
 * @return A short lowercase name
 */
auto TimeService::sourceLabel(TzOffsetSource source) -> const char* {
    switch (source) {
        case TZ_OFFSET_WEATHER:
            return "weather";
        case TZ_OFFSET_IP:
            return "ip";
        case TZ_OFFSET_CACHED:
            return "cached";
        case TZ_OFFSET_MANUAL:
        default:
            return "manual";
    }
}

/**
 * @brief Whether the clock has been set by NTP
 *
 * @return true once a plausible time is available
 */
auto TimeService::hasTime() -> bool { return time(nullptr) > REASONABLE_EPOCH; }

/**
 * @brief Current local wall-clock time
 *
 * @return The local time; valid is false when NTP has not synced yet
 */
auto TimeService::now() -> LocalTime {
    LocalTime local;

    const time_t utc = time(nullptr);
    if (utc <= REASONABLE_EPOCH) {
        return local;
    }

    const time_t shifted = utc + resolveOffsetSeconds();

    struct tm parts {};
    if (gmtime_r(&shifted, &parts) == nullptr) {
        return local;
    }

    local.valid = true;
    local.hour = parts.tm_hour;
    local.minute = parts.tm_min;
    local.second = parts.tm_sec;
    local.day = parts.tm_mday;
    local.month = parts.tm_mon + 1;
    local.year = parts.tm_year + TM_YEAR_BASE;
    local.weekday = parts.tm_wday;
    local.minutesOfDay = static_cast<int16_t>((parts.tm_hour * MINUTES_PER_HOUR) + parts.tm_min);

    return local;
}

/**
 * @brief Hour as it should be displayed, honouring the 12/24h setting
 *
 * @param local The local time
 * @param format12h Whether to use a 12-hour clock
 *
 * @return The hour to render
 */
auto TimeService::displayHour(const LocalTime& local, bool format12h) -> int {
    if (!format12h) {
        return local.hour;
    }

    const int hour = local.hour % HOURS_PER_HALF_DAY;

    return (hour == 0) ? HOURS_PER_HALF_DAY : hour;
}

/**
 * @brief Whether the local time is in the afternoon
 *
 * @param local The local time
 *
 * @return true for 12:00 and later
 */
auto TimeService::isPm(const LocalTime& local) -> bool { return local.hour >= HOURS_PER_HALF_DAY; }

/**
 * @brief Whether the separating colon should be drawn this instant
 *
 * With blinking enabled the colon is shown on even seconds, giving the
 * once-per-second flash the original firmware has.
 *
 * @param local The local time
 * @param blinkEnabled Whether blinking is enabled
 *
 * @return true when the colon should be visible
 */
auto TimeService::colonVisible(const LocalTime& local, bool blinkEnabled) -> bool {
    if (!blinkEnabled) {
        return true;
    }

    return (local.second % 2) == 0;
}

/**
 * @brief Format the time as HH:MM or HH:MM:SS
 *
 * @param local The local time
 * @param format12h Whether to use a 12-hour clock
 * @param showSeconds Whether to include seconds
 *
 * @return The formatted time, empty when the clock is not set
 */
auto TimeService::formatTime(const LocalTime& local, bool format12h, bool showSeconds) -> String {
    if (!local.valid) {
        return {"--:--"};
    }

    const int hour = TimeService::displayHour(local, format12h);

    std::array<char, TIME_BUF_SIZE> buf{};
    if (showSeconds) {
        snprintf(buf.data(), buf.size(), "%02d:%02d:%02d", hour, local.minute, local.second);
    } else {
        snprintf(buf.data(), buf.size(), "%02d:%02d", hour, local.minute);
    }

    return {buf.data()};
}

/**
 * @brief Format the date according to the configured pattern
 *
 * Supports the three patterns the web UI offers: DD/MM/YYYY, MM/DD/YYYY and
 * YYYY-MM-DD. Anything else falls back to DD/MM/YYYY.
 *
 * @param local The local time
 * @param dateFormat The pattern
 *
 * @return The formatted date, empty when the clock is not set
 */
auto TimeService::formatDate(const LocalTime& local, const char* dateFormat) -> String {
    if (!local.valid) {
        return {"--/--/----"};
    }

    std::array<char, DATE_BUF_SIZE> buf{};

    if (dateFormat != nullptr && strcmp(dateFormat, "MM/DD/YYYY") == 0) {
        snprintf(buf.data(), buf.size(), "%02d/%02d/%04d", local.month, local.day, local.year);
    } else if (dateFormat != nullptr && strcmp(dateFormat, "YYYY-MM-DD") == 0) {
        snprintf(buf.data(), buf.size(), "%04d-%02d-%02d", local.year, local.month, local.day);
    } else {
        snprintf(buf.data(), buf.size(), "%02d/%02d/%04d", local.day, local.month, local.year);
    }

    return {buf.data()};
}
