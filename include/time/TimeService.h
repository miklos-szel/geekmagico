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

#ifndef TIME_TIMESERVICE_H
#define TIME_TIMESERVICE_H

#include <Arduino.h>
#include <ctime>
#include <cstdint>

/**
 * @brief Where the UTC offset currently in effect came from
 *
 * Ordered by precedence: a higher value wins over a lower one, so a city
 * offset from the weather provider beats an IP lookup, which beats the value
 * cached from the last successful lookup, which beats the manual setting.
 */
enum TzOffsetSource : uint8_t {
    TZ_OFFSET_MANUAL = 0,   // The stored utc_offset_min
    TZ_OFFSET_CACHED = 1,   // Last auto offset, restored from config at boot
    TZ_OFFSET_IP = 2,       // Geolocation of the device's public IP
    TZ_OFFSET_WEATHER = 3,  // The configured city, as reported by OpenWeatherMap
};

/**
 * @brief Number of entries in TzOffsetSource
 */
static constexpr uint8_t TZ_OFFSET_SOURCE_COUNT = 4;

/**
 * @brief Local wall-clock fields derived from UTC plus the configured offset
 */
struct LocalTime {
    bool valid = false;
    int hour = 0;  // 0-23, always 24h; formatting applies 12h separately
    int minute = 0;
    int second = 0;
    int day = 0;
    int month = 0;  // 1-12
    int year = 0;   // full year
    int weekday = 0;
    int16_t minutesOfDay = -1;  // -1 when the time is not known yet
};

/**
 * @brief Timezone handling and clock formatting
 *
 * NTP stays synced to UTC (see NTPClient); the timezone offset is applied here
 * at format time, so changing the zone never forces a re-sync.
 */
class TimeService {
   public:
    static auto now() -> LocalTime;
    static auto hasTime() -> bool;

    static void setOffsetSeconds(TzOffsetSource source, int32_t offsetSeconds);
    static void clearOffsetSource(TzOffsetSource source);
    static void restoreCachedOffset();
    static auto offsetMinutes() -> int16_t;
    static auto activeSource() -> TzOffsetSource;
    static auto sourceLabel(TzOffsetSource source) -> const char*;

    static auto formatTime(const LocalTime& local, bool format12h, bool showSeconds) -> String;
    static auto formatDate(const LocalTime& local, const char* dateFormat) -> String;
    static auto displayHour(const LocalTime& local, bool format12h) -> int;
    static auto isPm(const LocalTime& local) -> bool;
    static auto colonVisible(const LocalTime& local, bool blinkEnabled) -> bool;
};

#endif  // TIME_TIMESERVICE_H
