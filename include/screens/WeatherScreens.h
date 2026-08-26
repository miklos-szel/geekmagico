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

#ifndef SCREENS_WEATHERSCREENS_H
#define SCREENS_WEATHERSCREENS_H

#include <Arduino.h>

#include "screens/Screen.h"
#include "weather/WeatherClient.h"

/**
 * @brief Clock with today's conditions and an 80x80 weather icon
 *
 * The icon slot has three modes, chosen by `weather.gif`: empty draws the
 * built-in vector glyph, "auto" plays the bundled animation matching the
 * current condition, and any other value plays that one file.
 */
class WeatherClockScreen : public Screen {
   public:
    void enter() override;
    void tick() override;
    void leave() override;

    auto needsWeather() const -> bool override { return true; }
    auto name() const -> const char* override { return "Weather Clock Today"; }

   private:
    String _timeCache;
    String _dateCache;
    String _tempCache;
    String _conditionCache;
    String _detailCache;
    String _noDataCache;

    // Condition the icon slot currently shows, whichever mode drew it. 0xFF
    // forces a repaint on the next tick.
    uint8_t _iconCondition = 0xFF;
    bool _gifStarted = false;
};

/**
 * @brief Multi-day forecast columns
 */
class WeatherForecastScreen : public Screen {
   public:
    void enter() override;
    void tick() override;

    auto needsWeather() const -> bool override { return true; }
    auto needsForecast() const -> bool override { return true; }
    auto name() const -> const char* override { return "Weather Forecast"; }

   private:
    static constexpr uint32_t REFRESH_INTERVAL_MS = 1800000UL;

    uint32_t _lastFetchMs = 0;
    bool _fetched = false;
    String _headerCache;
    std::array<String, FORECAST_DAYS> _rangeCache;
    std::array<uint8_t, FORECAST_DAYS> _conditionCache{};
};

/**
 * @brief Minimal clock with a single temperature line
 */
class SimpleWeatherClockScreen : public Screen {
   public:
    void enter() override;
    void tick() override;

    auto needsWeather() const -> bool override { return true; }
    auto name() const -> const char* override { return "Simple Weather Clock"; }

   private:
    String _timeCache;
    String _tempCache;
};

#endif  // SCREENS_WEATHERSCREENS_H
