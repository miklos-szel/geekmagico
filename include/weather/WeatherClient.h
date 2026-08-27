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

#ifndef WEATHER_WEATHERCLIENT_H
#define WEATHER_WEATHERCLIENT_H

#include <Arduino.h>
#include <array>
#include <cstdint>

/**
 * @brief Number of forecast days kept
 *
 * The keyless provider only returns three, so that is the useful ceiling.
 */
static constexpr uint8_t FORECAST_DAYS = 3;

/**
 * @brief Provider-independent weather condition
 *
 * Providers use incompatible code sets (OpenWeatherMap ids vs WWO codes), so
 * both are normalised to this enum and screens never see the difference.
 */
enum WeatherCondition : uint8_t {
    WX_UNKNOWN = 0,
    WX_CLEAR,
    WX_PARTLY_CLOUDY,
    WX_CLOUDY,
    WX_FOG,
    WX_DRIZZLE,
    WX_RAIN,
    WX_SNOW,
    WX_SLEET,
    WX_THUNDER,
};

/**
 * @brief Which upstream service produced the current reading
 */
enum WeatherProviderId : uint8_t {
    WX_PROVIDER_NONE = 0,
    WX_PROVIDER_OWM,
    WX_PROVIDER_KEYLESS,
};

/**
 * @brief Current conditions, always stored in SI units
 *
 * Unit preferences are applied when rendering, never here.
 */
struct WeatherNow {
    bool valid = false;
    float tempC = 0.0F;
    float feelsLikeC = 0.0F;
    float windMs = 0.0F;
    uint16_t pressureHpa = 0;
    uint8_t humidity = 0;
    WeatherCondition condition = WX_UNKNOWN;
    std::array<char, 24> description{};

    // Night at the observed location, as reported by the provider. Unrelated
    // to Backlight::isNightActive(), which is the user's configured dimming
    // window; the two only coincide by accident.
    bool isNight = false;

    bool tzOffsetKnown = false;
    int32_t tzOffsetSeconds = 0;
};

/**
 * @brief One forecast day
 */
struct ForecastDay {
    bool valid = false;
    float minC = 0.0F;
    float maxC = 0.0F;
    WeatherCondition condition = WX_UNKNOWN;
};

/**
 * @brief Multi-day forecast
 */
struct WeatherForecast {
    bool valid = false;
    std::array<ForecastDay, FORECAST_DAYS> days{};
};

/**
 * @brief Fetches weather over plain HTTP
 *
 * OpenWeatherMap is used when an API key is configured; otherwise a keyless
 * provider keeps the device useful out of the box. Neither uses TLS, which is
 * what keeps BearSSL's heap cost out of the firmware.
 */
class WeatherClient {
   public:
    static void begin();
    static void loop();

    static auto refreshNow() -> bool;
    static auto refreshForecast() -> bool;

    static auto current() -> const WeatherNow&;
    static auto forecast() -> const WeatherForecast&;

    static auto providerInUse() -> WeatherProviderId;
    static auto lastStatus() -> String;
    static auto lastSuccessMs() -> uint32_t;

    static auto conditionLabel(WeatherCondition condition) -> const char*;

    static auto toDisplayTemp(float celsius) -> float;
    static auto toDisplayWind(float metersPerSecond) -> float;
    static auto toDisplayPressure(uint16_t hectopascals) -> float;
    static auto temperatureUnit() -> const char*;
    static auto windUnit() -> const char*;
    static auto pressureUnit() -> const char*;
};

#endif  // WEATHER_WEATHERCLIENT_H
