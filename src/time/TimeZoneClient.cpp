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
#include <ArduinoJson.h>
#include <Logger.h>
#include <cstdint>
#include <cstring>

#include "time/TimeZoneClient.h"
#include "net/HttpJson.h"
#include "config/ConfigManager.h"
#include "time/TimeService.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;

// ArduinoJson's obj["key"] accessors trip readability-misplaced-array-index;
// the check misreads the proxy type's subscript.
// NOLINTBEGIN(readability-misplaced-array-index)

namespace {

constexpr const char* TAG = "Timezone";

// Free tier is plain HTTP only and rate limited to 45 requests per minute;
// one request every few hours is nowhere near it.
constexpr const char* LOOKUP_URL = "http://ip-api.com/json/?fields=status,offset";

constexpr uint32_t REFRESH_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t RETRY_DELAY_MS = 60000UL;
constexpr int32_t OFFSET_ABSENT = INT32_MIN;
constexpr int32_t MAX_OFFSET_SECONDS = 14 * 3600;
constexpr int32_t MIN_OFFSET_SECONDS = -12 * 3600;
constexpr int32_t SECONDS_PER_MINUTE = 60;

String statusMessage;
uint32_t nextAttemptAtMs = 0;

/**
 * @brief Whether an IP lookup is worth making right now
 *
 * Manual mode never needs one, and a city offset from OpenWeatherMap is more
 * accurate than geolocating the public IP, so it wins outright.
 *
 * @return true when the lookup should run
 */
auto lookupNeeded() -> bool {
    if (configManager.settings.time.tz_mode == "manual") {
        return false;
    }

    return TimeService::activeSource() != TZ_OFFSET_WEATHER;
}

}  // namespace

/**
 * @brief Prepare the client and schedule the first lookup
 *
 * @return void
 */
void TimeZoneClient::begin() {
    statusMessage = "waiting for first lookup";
    nextAttemptAtMs = millis();
}

/**
 * @brief Refresh the IP-derived offset if the interval has elapsed
 *
 * @return void
 */
void TimeZoneClient::loop() {
    if (static_cast<int32_t>(millis() - nextAttemptAtMs) < 0) {
        return;
    }

    if (!WiFiManager::isConnected()) {
        nextAttemptAtMs = millis() + RETRY_DELAY_MS;
        return;
    }

    if (!lookupNeeded()) {
        // Costs nothing to ask again shortly: switching to auto mode, or the
        // weather provider losing its offset, should not wait out a six-hour
        // interval before the clock corrects itself.
        nextAttemptAtMs = millis() + RETRY_DELAY_MS;
        return;
    }

    const bool resolved = TimeZoneClient::refreshNow();

    nextAttemptAtMs = millis() + (resolved ? REFRESH_INTERVAL_MS : RETRY_DELAY_MS);
}

/**
 * @brief Look the offset up now
 *
 * A failure leaves the current offset alone, so a flaky lookup never drops a
 * working clock back to UTC.
 *
 * @return true when an offset was resolved
 */
auto TimeZoneClient::refreshNow() -> bool {
    JsonDocument filter;
    filter["status"] = true;
    filter["offset"] = true;

    JsonDocument doc;
    if (!HttpJson::fetchFiltered(LOOKUP_URL, filter, doc, statusMessage)) {
        return false;
    }

    if (strcmp(doc["status"] | "", "success") != 0) {
        statusMessage = "lookup failed";
        return false;
    }

    // Sentinel rather than is<int32_t>(): the type test is strict about how
    // the number was parsed, and a missed offset is silently wrong time.
    const int32_t offsetSeconds = doc["offset"] | OFFSET_ABSENT;
    if (offsetSeconds == OFFSET_ABSENT || offsetSeconds < MIN_OFFSET_SECONDS || offsetSeconds > MAX_OFFSET_SECONDS) {
        statusMessage = "no usable offset";
        return false;
    }

    TimeService::setOffsetSeconds(TZ_OFFSET_IP, offsetSeconds);

    Logger::info((String("Timezone resolved from IP: ") + String(offsetSeconds / SECONDS_PER_MINUTE) + " min").c_str(),
                 TAG);

    return true;
}

/**
 * @brief Result of the last lookup, for the API and the web UI
 *
 * @return The status message
 */
auto TimeZoneClient::lastStatus() -> const String& { return statusMessage; }

// NOLINTEND(readability-misplaced-array-index)
