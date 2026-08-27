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
#include <ESP8266WiFi.h>
#include <Logger.h>
#include <algorithm>
#include <cstring>

#include "weather/WeatherClient.h"
#include "net/HttpJson.h"
#include "config/ConfigManager.h"
#include "time/TimeService.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;

// ArduinoJson's obj["key"] accessors trip readability-misplaced-array-index
// throughout this file; the check misreads the proxy type's subscript. The
// suppression is scoped to this file rather than repeated on every line.
// NOLINTBEGIN(readability-misplaced-array-index)

namespace {

constexpr const char* TAG = "Weather";

constexpr uint32_t MS_PER_MINUTE = 60000UL;
constexpr uint16_t MIN_INTERVAL_KEYLESS_MIN = 30;
constexpr uint16_t MIN_INTERVAL_KEYED_MIN = 1;
constexpr uint32_t RETRY_DELAY_MS = 60000UL;
constexpr int32_t TZ_OFFSET_ABSENT = INT32_MIN;

// Bounds of the "it is daytime" band for the clock fallback in
// nightFromClock(), used only when a provider reports no day/night flag.
constexpr int DAYLIGHT_START_HOUR = 6;
constexpr int DAYLIGHT_END_HOUR = 20;

constexpr float MS_TO_KMH = 3.6F;
constexpr float MS_TO_MPH = 2.236936F;
constexpr float KMH_TO_MS = 1.0F / 3.6F;
constexpr float HPA_TO_INHG = 0.02952998F;
constexpr float HPA_TO_MMHG = 0.7500617F;
constexpr float C_TO_F_SCALE = 9.0F / 5.0F;
constexpr float C_TO_F_OFFSET = 32.0F;

// OpenWeatherMap condition id bands
constexpr uint16_t OWM_THUNDER_MIN = 200;
constexpr uint16_t OWM_DRIZZLE_MIN = 300;
constexpr uint16_t OWM_RAIN_MIN = 500;
constexpr uint16_t OWM_SNOW_MIN = 600;
constexpr uint16_t OWM_ATMOSPHERE_MIN = 700;
constexpr uint16_t OWM_CLEAR = 800;
constexpr uint16_t OWM_FEW_CLOUDS = 801;
constexpr uint16_t OWM_SCATTERED_CLOUDS = 802;
constexpr uint16_t OWM_BROKEN_CLOUDS = 803;
constexpr uint16_t OWM_OVERCAST = 804;
constexpr uint16_t OWM_FREEZING_RAIN = 511;
constexpr size_t CITY_ENCODE_SLACK = 8;

WeatherNow currentWeather;
WeatherForecast currentForecast;
WeatherProviderId activeProvider = WX_PROVIDER_NONE;
String statusMessage = "not started";
uint32_t lastSuccessAtMs = 0;
uint32_t nextAttemptAtMs = 0;
bool everFetched = false;

/**
 * @brief Whether the configured city looks like a numeric OpenWeatherMap id
 */
auto cityIsNumeric(const std::string& city) -> bool {
    if (city.empty()) {
        return false;
    }

    return std::all_of(city.begin(), city.end(), [](char chr) { return isdigit(static_cast<unsigned char>(chr)) != 0; });
}

/**
 * @brief Percent-encode the characters that actually appear in city names
 */
auto encodeCity(const std::string& city) -> String {
    String encoded;
    encoded.reserve(city.size() + CITY_ENCODE_SLACK);

    for (const char chr : city) {
        if (isalnum(static_cast<unsigned char>(chr)) != 0 || chr == '-' || chr == '_' || chr == '.' || chr == ',') {
            encoded += chr;
        } else if (chr == ' ') {
            encoded += "%20";
        }
    }

    return encoded;
}

/**
 * @brief Whether an OpenWeatherMap icon name denotes night
 *
 * The names are three characters -- two digits and a 'd' or 'n' suffix, as in
 * "01d" / "01n". Anything shorter or absent is treated as daytime, which is
 * the safer default: a sun at night reads as stale data, a moon at noon reads
 * as a bug.
 *
 * @param icon Icon name from weather[0].icon, possibly empty
 *
 * @return true when the name ends in 'n'
 */
auto iconSuffixIsNight(const char* icon) -> bool {
    if (icon == nullptr) {
        return false;
    }

    const size_t len = strlen(icon);

    return len != 0 && icon[len - 1] == 'n';
}

/**
 * @brief Whether a World Weather Online symbol URL denotes night
 *
 * wttr.in carries no day flag of its own, but the symbol it links to is named
 * for the variant -- "wsymbol_0008_clear_sky_night" against
 * "wsymbol_0001_sunny" -- so the substring is the signal.
 *
 * @param url Value of current_condition[0].weatherIconUrl[0].value
 *
 * @return true when the URL names a night symbol
 */
auto iconUrlIsNight(const char* url) -> bool {
    return url != nullptr && strstr(url, "night") != nullptr;
}

/**
 * @brief Crude day/night guess from the device clock
 *
 * Only used when a provider gives no day/night signal at all. Solar noon moves
 * and the poles ignore this entirely, so it is a fallback and not a
 * replacement: it is wrong for a few hours a year at temperate latitudes and
 * wrong for months inside the Arctic circle.
 *
 * @return true when the local hour is outside the daylight band, false when
 *         the clock has not been set yet
 */
auto nightFromClock() -> bool {
    const LocalTime local = TimeService::now();
    if (!local.valid) {
        return false;
    }

    return local.hour < DAYLIGHT_START_HOUR || local.hour >= DAYLIGHT_END_HOUR;
}

/**
 * @brief Map an OpenWeatherMap condition id to the normalised enum
 */
auto conditionFromOwm(uint16_t code) -> WeatherCondition {
    if (code == OWM_CLEAR) {
        return WX_CLEAR;
    }
    if (code == OWM_FEW_CLOUDS || code == OWM_SCATTERED_CLOUDS) {
        return WX_PARTLY_CLOUDY;
    }
    if (code == OWM_BROKEN_CLOUDS || code == OWM_OVERCAST) {
        return WX_CLOUDY;
    }
    if (code == OWM_FREEZING_RAIN) {
        return WX_SLEET;
    }
    if (code >= OWM_THUNDER_MIN && code < OWM_DRIZZLE_MIN) {
        return WX_THUNDER;
    }
    if (code >= OWM_DRIZZLE_MIN && code < OWM_RAIN_MIN) {
        return WX_DRIZZLE;
    }
    if (code >= OWM_RAIN_MIN && code < OWM_SNOW_MIN) {
        return WX_RAIN;
    }
    if (code >= OWM_SNOW_MIN && code < OWM_ATMOSPHERE_MIN) {
        return WX_SNOW;
    }
    if (code >= OWM_ATMOSPHERE_MIN && code < OWM_CLEAR) {
        return WX_FOG;
    }

    return WX_UNKNOWN;
}

/**
 * @brief Map a World Weather Online code (used by the keyless provider)
 *
 * These are an externally defined code set, so the numbers are data rather
 * than magic constants; the table keeps them readable next to their meaning.
 */
// NOLINTBEGIN(readability-magic-numbers)
struct WwoMapping {
    uint16_t code;
    WeatherCondition condition;
};

constexpr std::array<WwoMapping, 40> WWO_MAPPINGS = {{
    {113, WX_CLEAR},
    {116, WX_PARTLY_CLOUDY},
    {119, WX_CLOUDY},   {122, WX_CLOUDY},
    {143, WX_FOG},      {248, WX_FOG},      {260, WX_FOG},
    {176, WX_DRIZZLE},  {263, WX_DRIZZLE},  {266, WX_DRIZZLE},
    {293, WX_DRIZZLE},  {296, WX_DRIZZLE},
    {299, WX_RAIN},     {302, WX_RAIN},     {305, WX_RAIN},
    {308, WX_RAIN},     {353, WX_RAIN},     {356, WX_RAIN},
    {359, WX_RAIN},
    {179, WX_SNOW},     {227, WX_SNOW},     {230, WX_SNOW},
    {323, WX_SNOW},     {326, WX_SNOW},     {329, WX_SNOW},
    {332, WX_SNOW},     {335, WX_SNOW},     {338, WX_SNOW},
    {368, WX_SNOW},     {371, WX_SNOW},
    {182, WX_SLEET},    {185, WX_SLEET},    {281, WX_SLEET},
    {284, WX_SLEET},    {311, WX_SLEET},
    {200, WX_THUNDER},  {386, WX_THUNDER},  {389, WX_THUNDER},
    {392, WX_THUNDER},  {395, WX_THUNDER},
}};
// NOLINTEND(readability-magic-numbers)

auto conditionFromWwo(uint16_t code) -> WeatherCondition {
    for (const auto& mapping : WWO_MAPPINGS) {
        if (mapping.code == code) {
            return mapping.condition;
        }
    }

    return WX_UNKNOWN;
}

/**
 * @brief Copy a description into the fixed-size buffer, always NUL terminated
 */
void setDescription(WeatherNow& out, const char* text) {
    out.description.fill('\0');

    if (text == nullptr) {
        return;
    }

    strncpy(out.description.data(), text, out.description.size() - 1);
}

/**
 * @brief Effective refresh interval, floored to protect the upstream service
 */
auto effectiveIntervalMs() -> uint32_t {
    const bool keyed = !configManager.settings.weather.api_key.empty();
    const uint16_t floorMin = keyed ? MIN_INTERVAL_KEYED_MIN : MIN_INTERVAL_KEYLESS_MIN;
    const uint16_t configured = std::max(floorMin, configManager.settings.weather.interval_min);

    return static_cast<uint32_t>(configured) * MS_PER_MINUTE;
}

/**
 * @brief Run a filtered GET and record the outcome for the status endpoint
 *
 * @param url The URL to fetch
 * @param filter Filter describing the fields to keep
 * @param doc Document receiving the filtered result
 *
 * @return true when the request and parse both succeeded
 */
auto fetchFiltered(const String& url, JsonDocument& filter, JsonDocument& doc) -> bool {
    return HttpJson::fetchFiltered(url, filter, doc, statusMessage);
}

/**
 * @brief Fetch current conditions from OpenWeatherMap
 */
auto fetchOwmNow(WeatherNow& out) -> bool {
    const WeatherSettings& cfg = configManager.settings.weather;

    String url = "http://api.openweathermap.org/data/2.5/weather?";
    url += cityIsNumeric(configManager.settings.city) ? "id=" : "q=";
    url += encodeCity(configManager.settings.city);
    url += "&units=metric&appid=";
    url += cfg.api_key.c_str();

    JsonDocument filter;
    filter["main"]["temp"] = true;
    filter["main"]["feels_like"] = true;
    filter["main"]["humidity"] = true;
    filter["main"]["pressure"] = true;
    filter["wind"]["speed"] = true;
    filter["weather"][0]["id"] = true;
    filter["weather"][0]["description"] = true;
    filter["weather"][0]["icon"] = true;
    filter["timezone"] = true;

    JsonDocument doc;
    if (!fetchFiltered(url, filter, doc)) {
        return false;
    }

    JsonObjectConst main = doc["main"];
    if (main.isNull()) {
        statusMessage = "unexpected response";
        return false;
    }

    out.tempC = main["temp"] | 0.0F;
    out.feelsLikeC = main["feels_like"] | out.tempC;
    out.humidity = main["humidity"] | 0;
    out.pressureHpa = main["pressure"] | 0;
    out.windMs = doc["wind"]["speed"] | 0.0F;
    out.condition = conditionFromOwm(doc["weather"][0]["id"] | 0);
    setDescription(out, doc["weather"][0]["description"] | "");

    // OWM's icon name is the only day/night signal in this payload: "01d" by
    // day, "01n" by night. Read the suffix rather than indexing blind, so a
    // shortened or absent value leaves the flag on its daytime default.
    out.isNight = iconSuffixIsNight(doc["weather"][0]["icon"] | "");

    // OpenWeatherMap reports the city's UTC offset, which is what makes the
    // "auto" timezone mode possible.
    // Sentinel rather than is<int32_t>(): the type test is strict about how
    // the number was parsed, and a missed offset silently leaves auto mode on
    // whatever fallback is in place.
    const int32_t tzOffset = doc["timezone"] | TZ_OFFSET_ABSENT;
    if (tzOffset != TZ_OFFSET_ABSENT) {
        out.tzOffsetSeconds = tzOffset;
        out.tzOffsetKnown = true;
    }

    out.valid = true;

    return true;
}

/**
 * @brief Fetch current conditions from the keyless provider
 *
 * Every value in this feed is a JSON string, so each one is converted rather
 * than read as a number.
 */
auto fetchKeylessNow(WeatherNow& out) -> bool {
    String url = "http://wttr.in/";
    url += encodeCity(configManager.settings.city);
    url += "?format=j1";

    JsonDocument filter;
    JsonObject condition = filter["current_condition"].add<JsonObject>();
    condition["temp_C"] = true;
    condition["FeelsLikeC"] = true;
    condition["humidity"] = true;
    condition["pressure"] = true;
    condition["windspeedKmph"] = true;
    condition["weatherCode"] = true;
    condition["weatherDesc"][0]["value"] = true;
    condition["weatherIconUrl"][0]["value"] = true;

    JsonDocument doc;
    if (!fetchFiltered(url, filter, doc)) {
        return false;
    }

    JsonObjectConst reading = doc["current_condition"][0];
    if (reading.isNull()) {
        statusMessage = "unexpected response";
        return false;
    }

    out.tempC = String(reading["temp_C"] | "0").toFloat();
    out.feelsLikeC = String(reading["FeelsLikeC"] | "0").toFloat();
    out.humidity = static_cast<uint8_t>(String(reading["humidity"] | "0").toInt());
    out.pressureHpa = static_cast<uint16_t>(String(reading["pressure"] | "0").toInt());
    out.windMs = String(reading["windspeedKmph"] | "0").toFloat() * KMH_TO_MS;
    out.condition = conditionFromWwo(static_cast<uint16_t>(String(reading["weatherCode"] | "0").toInt()));

    String description = reading["weatherDesc"][0]["value"] | "";
    description.trim();
    setDescription(out, description.c_str());

    // The symbol URL is this feed's only day/night signal, and it is not
    // guaranteed to be present. When it is missing, fall back to the clock
    // rather than pinning every keyless install to daytime icons.
    const char* iconUrl = reading["weatherIconUrl"][0]["value"] | "";
    out.isNight = (iconUrl[0] != '\0') ? iconUrlIsNight(iconUrl) : nightFromClock();

    // This feed carries no UTC offset, so "auto" timezone cannot be resolved
    // from it; the configured manual offset stays in effect.
    out.tzOffsetKnown = false;
    out.valid = true;

    return true;
}

}  // namespace

/**
 * @brief Prepare the client and schedule the first fetch
 *
 * @return void
 */
void WeatherClient::begin() {
    statusMessage = "waiting for first fetch";
    nextAttemptAtMs = millis();
}

/**
 * @brief Refresh current conditions if the interval has elapsed
 *
 * @return void
 */
void WeatherClient::loop() {
    if (static_cast<int32_t>(millis() - nextAttemptAtMs) < 0) {
        return;
    }

    if (!WiFiManager::isConnected()) {
        nextAttemptAtMs = millis() + RETRY_DELAY_MS;
        return;
    }

    const bool refreshed = WeatherClient::refreshNow();

    nextAttemptAtMs = millis() + (refreshed ? effectiveIntervalMs() : RETRY_DELAY_MS);
}

/**
 * @brief Fetch current conditions now
 *
 * Uses OpenWeatherMap when a key is configured, otherwise the keyless
 * provider. A failure leaves the previous reading in place so screens keep
 * showing the last known values rather than blanking.
 *
 * @return true on success
 */
auto WeatherClient::refreshNow() -> bool {
    if (configManager.settings.city.empty()) {
        statusMessage = "no city configured";
        return false;
    }

    const bool keyed = !configManager.settings.weather.api_key.empty();

    WeatherNow fetched;
    const bool fetchOk = keyed ? fetchOwmNow(fetched) : fetchKeylessNow(fetched);

    if (!fetchOk) {
        Logger::warn((String("Weather fetch failed: ") + statusMessage).c_str(), TAG);
        return false;
    }

    currentWeather = fetched;
    activeProvider = keyed ? WX_PROVIDER_OWM : WX_PROVIDER_KEYLESS;
    lastSuccessAtMs = millis();
    everFetched = true;
    statusMessage = "ok";

    if (currentWeather.tzOffsetKnown) {
        TimeService::setOffsetSeconds(TZ_OFFSET_WEATHER, currentWeather.tzOffsetSeconds);
    } else {
        TimeService::clearOffsetSource(TZ_OFFSET_WEATHER);
    }

    Logger::info((String("Weather updated: ") + String(currentWeather.tempC, 1) + "C " +
                  WeatherClient::conditionLabel(currentWeather.condition))
                     .c_str(),
                 TAG);

    return true;
}

/**
 * @brief Fetch the multi-day forecast
 *
 * Deliberately separate from the current-conditions fetch and only called by
 * the forecast screen, so the extra request and parse cost nothing when that
 * screen is not being shown.
 *
 * @return true on success
 */
auto WeatherClient::refreshForecast() -> bool {
    if (configManager.settings.city.empty()) {
        statusMessage = "no city configured";
        return false;
    }

    const WeatherSettings& cfg = configManager.settings.weather;
    const std::string& key = cfg.forecast_key.empty() ? cfg.api_key : cfg.forecast_key;

    WeatherForecast fetched;

    if (!key.empty()) {
        String url = "http://api.openweathermap.org/data/2.5/forecast?";
        url += cityIsNumeric(configManager.settings.city) ? "id=" : "q=";
        url += encodeCity(configManager.settings.city);
        url += "&units=metric&cnt=24&appid=";
        url += key.c_str();

        JsonDocument filter;
        JsonObject entry = filter["list"].add<JsonObject>();
        entry["dt"] = true;
        entry["main"]["temp_min"] = true;
        entry["main"]["temp_max"] = true;
        entry["weather"][0]["id"] = true;

        JsonDocument doc;
        if (!fetchFiltered(url, filter, doc)) {
            return false;
        }

        constexpr uint32_t SECONDS_PER_DAY = 86400;
        uint32_t firstDay = 0;

        for (JsonObjectConst item : doc["list"].as<JsonArrayConst>()) {
            const uint32_t timestamp = item["dt"] | 0U;
            if (timestamp == 0) {
                continue;
            }

            const uint32_t dayIndexAbsolute = timestamp / SECONDS_PER_DAY;
            if (firstDay == 0) {
                firstDay = dayIndexAbsolute;
            }

            const uint32_t slot = dayIndexAbsolute - firstDay;
            if (slot >= FORECAST_DAYS) {
                continue;
            }

            ForecastDay& day = fetched.days[slot];
            const float minC = item["main"]["temp_min"] | 0.0F;
            const float maxC = item["main"]["temp_max"] | 0.0F;

            if (!day.valid) {
                day.valid = true;
                day.minC = minC;
                day.maxC = maxC;
                day.condition = conditionFromOwm(item["weather"][0]["id"] | 0);
            } else {
                day.minC = std::min(day.minC, minC);
                day.maxC = std::max(day.maxC, maxC);
            }

            EspClass::wdtFeed();
        }
    } else {
        String url = "http://wttr.in/";
        url += encodeCity(configManager.settings.city);
        url += "?format=j1";

        JsonDocument filter;
        JsonObject day = filter["weather"].add<JsonObject>();
        day["maxtempC"] = true;
        day["mintempC"] = true;
        day["hourly"][0]["weatherCode"] = true;

        JsonDocument doc;
        if (!fetchFiltered(url, filter, doc)) {
            return false;
        }

        uint8_t slot = 0;
        for (JsonObjectConst item : doc["weather"].as<JsonArrayConst>()) {
            if (slot >= FORECAST_DAYS) {
                break;
            }

            ForecastDay& day = fetched.days[slot];
            day.valid = true;
            day.minC = String(item["mintempC"] | "0").toFloat();
            day.maxC = String(item["maxtempC"] | "0").toFloat();

            // Midday is more representative of a day than midnight.
            JsonArrayConst hourly = item["hourly"].as<JsonArrayConst>();
            const size_t middaySlot = hourly.size() / 2;
            day.condition =
                conditionFromWwo(static_cast<uint16_t>(String(hourly[middaySlot]["weatherCode"] | "0").toInt()));

            slot++;
            EspClass::wdtFeed();
        }
    }

    fetched.valid = fetched.days[0].valid;
    if (!fetched.valid) {
        statusMessage = "empty forecast";
        return false;
    }

    currentForecast = fetched;
    statusMessage = "ok";

    return true;
}

/**
 * @brief Most recent current-conditions reading
 *
 * @return The reading; valid is false until the first successful fetch
 */
auto WeatherClient::current() -> const WeatherNow& { return currentWeather; }

/**
 * @brief Most recent forecast
 *
 * @return The forecast; valid is false until the first successful fetch
 */
auto WeatherClient::forecast() -> const WeatherForecast& { return currentForecast; }

/**
 * @brief Which provider produced the current reading
 *
 * @return The provider id
 */
auto WeatherClient::providerInUse() -> WeatherProviderId { return activeProvider; }

/**
 * @brief Human readable status of the last fetch attempt
 *
 * @return The status message
 */
auto WeatherClient::lastStatus() -> String { return everFetched ? statusMessage : String("no data yet"); }

/**
 * @brief millis() timestamp of the last successful fetch
 *
 * @return The timestamp, 0 when nothing has succeeded yet
 */
auto WeatherClient::lastSuccessMs() -> uint32_t { return lastSuccessAtMs; }

/**
 * @brief Short label for a condition
 *
 * @param condition The condition
 *
 * @return A display label
 */
auto WeatherClient::conditionLabel(WeatherCondition condition) -> const char* {
    switch (condition) {
        case WX_CLEAR:
            return "Clear";
        case WX_PARTLY_CLOUDY:
            return "Partly cloudy";
        case WX_CLOUDY:
            return "Cloudy";
        case WX_FOG:
            return "Fog";
        case WX_DRIZZLE:
            return "Drizzle";
        case WX_RAIN:
            return "Rain";
        case WX_SNOW:
            return "Snow";
        case WX_SLEET:
            return "Sleet";
        case WX_THUNDER:
            return "Thunder";
        case WX_UNKNOWN:
        default:
            return "--";
    }
}

/**
 * @brief Convert a stored temperature to the configured display unit
 *
 * @param celsius Temperature in degrees Celsius
 *
 * @return Temperature in the display unit
 */
auto WeatherClient::toDisplayTemp(float celsius) -> float {
    if (configManager.settings.weather.temp == "f") {
        return (celsius * C_TO_F_SCALE) + C_TO_F_OFFSET;
    }

    return celsius;
}

/**
 * @brief Convert a stored wind speed to the configured display unit
 *
 * @param metersPerSecond Wind speed in metres per second
 *
 * @return Wind speed in the display unit
 */
auto WeatherClient::toDisplayWind(float metersPerSecond) -> float {
    const std::string& unit = configManager.settings.weather.wind;

    if (unit == "kmh") {
        return metersPerSecond * MS_TO_KMH;
    }
    if (unit == "mph") {
        return metersPerSecond * MS_TO_MPH;
    }

    return metersPerSecond;
}

/**
 * @brief Convert a stored pressure to the configured display unit
 *
 * @param hectopascals Pressure in hectopascals
 *
 * @return Pressure in the display unit
 */
auto WeatherClient::toDisplayPressure(uint16_t hectopascals) -> float {
    const std::string& unit = configManager.settings.weather.pressure;
    const auto value = static_cast<float>(hectopascals);

    if (unit == "inhg") {
        return value * HPA_TO_INHG;
    }
    if (unit == "mmhg") {
        return value * HPA_TO_MMHG;
    }

    return value;
}

/**
 * @brief Label for the configured temperature unit
 *
 * @return The unit label
 */
auto WeatherClient::temperatureUnit() -> const char* {
    return (configManager.settings.weather.temp == "f") ? "F" : "C";
}

/**
 * @brief Label for the configured wind unit
 *
 * @return The unit label
 */
auto WeatherClient::windUnit() -> const char* {
    const std::string& unit = configManager.settings.weather.wind;

    if (unit == "kmh") {
        return "km/h";
    }
    if (unit == "mph") {
        return "mph";
    }

    return "m/s";
}

/**
 * @brief Label for the configured pressure unit
 *
 * @return The unit label
 */
auto WeatherClient::pressureUnit() -> const char* {
    const std::string& unit = configManager.settings.weather.pressure;

    if (unit == "inhg") {
        return "inHg";
    }
    if (unit == "mmhg") {
        return "mmHg";
    }

    return "hPa";
}

// NOLINTEND(readability-misplaced-array-index)
