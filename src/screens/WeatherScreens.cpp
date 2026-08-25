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
#include <LittleFS.h>

#include "screens/WeatherScreens.h"
#include "screens/DrawUtils.h"
#include "display/DisplayManager.h"
#include "config/ConfigManager.h"
#include "time/TimeService.h"
#include "wireless/WiFiManager.h"

extern ConfigManager configManager;

namespace {
constexpr uint16_t BG_COLOR = LCD_BLACK;
constexpr uint16_t MUTED = 0x8410;
constexpr uint16_t ACCENT = 0xFD20;

// Panel is a fixed 240x240, so the layouts are written as literal geometry.
// NOLINTBEGIN(readability-magic-numbers)
constexpr int16_t PANEL_W = 240;
constexpr int16_t GIF_BOX = 80;
// NOLINTEND(readability-magic-numbers)

/**
 * @brief Format the current temperature for display
 */
auto formatTemp(float celsius) -> String {
    return String(WeatherClient::toDisplayTemp(celsius), 0) + String(WeatherClient::temperatureUnit());
}
}  // namespace

// NOLINTBEGIN(readability-magic-numbers)

/**
 * @brief Paint the weather clock chrome and start the weather GIF if set
 *
 * @return void
 */
void WeatherClockScreen::enter() {
    DisplayManager::getGfx()->fillScreen(BG_COLOR);

    _timeCache = "";
    _dateCache = "";
    _tempCache = "";
    _conditionCache = "";
    _detailCache = "";
    _noDataCache = "";
    _lastCondition = 0xFF;
    _gifStarted = false;

    // The 80x80 animation from the Weather page, when the user picked one.
    const std::string& gifName = configManager.settings.weather.gif;
    if (!gifName.empty()) {
        const String path = String("/gif/") + gifName.c_str();
        if (LittleFS.exists(path)) {
            _gifStarted = DisplayManager::playGifAt(path, 8, 116, GIF_BOX, GIF_BOX);
        }
    }
}

/**
 * @brief Stop any GIF the screen started
 *
 * @return void
 */
void WeatherClockScreen::leave() {
    if (_gifStarted) {
        DisplayManager::stopGifQuiet();
        _gifStarted = false;
    }
}

/**
 * @brief Repaint the changed parts of the weather clock
 *
 * @return void
 */
void WeatherClockScreen::tick() {
    const LocalTime local = TimeService::now();
    const TimeSettings& timeCfg = configManager.settings.time;
    const WeatherNow& weather = WeatherClient::current();

    DrawUtils::cachedText(PANEL_W / 2, 14, TimeService::formatTime(local, timeCfg.format12h, false), _timeCache, 5,
                          timeCfg.hour_color, BG_COLOR, ALIGN_CENTER);

    DrawUtils::cachedText(PANEL_W / 2, 62, TimeService::formatDate(local, timeCfg.date_format.c_str()), _dateCache, 2,
                          MUTED, BG_COLOR, ALIGN_CENTER);

    if (!weather.valid) {
        // Clear whatever was painted the last time weather was valid so it
        // doesn't linger behind the fallback message.
        DrawUtils::cachedText(232, 120, "", _tempCache, 5, ACCENT, BG_COLOR, ALIGN_RIGHT);
        DrawUtils::cachedText(232, 168, "", _conditionCache, 2, MUTED, BG_COLOR, ALIGN_RIGHT);
        DrawUtils::cachedText(232, 196, "", _detailCache, 2, MUTED, BG_COLOR, ALIGN_RIGHT);

        if (!_gifStarted && _lastCondition != 0xFF) {
            DisplayManager::getGfx()->fillRect(8, 116, GIF_BOX, GIF_BOX, BG_COLOR);
            _lastCondition = 0xFF;
        }

        DrawUtils::cachedText(PANEL_W / 2, 120, "No weather data", _noDataCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
        return;
    }

    if (_noDataCache.length() != 0) {
        DrawUtils::cachedText(PANEL_W / 2, 120, "", _noDataCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
    }

    // When no GIF is configured the icon takes the slot the GIF would occupy.
    if (!_gifStarted && weather.condition != _lastCondition) {
        DrawUtils::weatherGlyph(8, 116, GIF_BOX, weather.condition, BG_COLOR);
        _lastCondition = weather.condition;
    }

    DrawUtils::cachedText(232, 120, formatTemp(weather.tempC), _tempCache, 5, ACCENT, BG_COLOR, ALIGN_RIGHT);
    DrawUtils::cachedText(232, 168, WeatherClient::conditionLabel(weather.condition), _conditionCache, 2, MUTED, BG_COLOR,
                          ALIGN_RIGHT);

    const String detail = String("H") + String(weather.humidity) + "%  " +
                          String(WeatherClient::toDisplayWind(weather.windMs), 1) + WeatherClient::windUnit();
    DrawUtils::cachedText(232, 196, detail, _detailCache, 2, MUTED, BG_COLOR, ALIGN_RIGHT);
}

/**
 * @brief Paint the forecast chrome and request a forecast
 *
 * @return void
 */
void WeatherForecastScreen::enter() {
    auto* gfx = DisplayManager::getGfx();
    gfx->fillScreen(BG_COLOR);

    _headerCache = "";
    _rangeCache.fill(String());
    _conditionCache.fill(0xFF);

    gfx->fillRect(0, 0, PANEL_W, 30, 0x18E3);
    DrawUtils::drawText(PANEL_W / 2, 11, "FORECAST", 2, LCD_WHITE, 0x18E3, ALIGN_CENTER);

    // Fetching here rather than in tick() keeps the second HTTP request off
    // the loop for every theme that does not show a forecast.
    if (!_fetched || (millis() - _lastFetchMs) > REFRESH_INTERVAL_MS) {
        if (WiFiManager::isConnected() && WeatherClient::refreshForecast()) {
            _fetched = true;
            _lastFetchMs = millis();
        }
    }
}

/**
 * @brief Repaint the forecast columns
 *
 * @return void
 */
void WeatherForecastScreen::tick() {
    const WeatherForecast& forecast = WeatherClient::forecast();

    if (!forecast.valid) {
        DrawUtils::cachedText(PANEL_W / 2, 110, "No forecast data", _headerCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
        return;
    }

    static constexpr std::array<const char*, FORECAST_DAYS> LABELS = {"TODAY", "TOMORROW", "IN 2 DAYS"};
    constexpr int16_t rowH = 66;
    constexpr int16_t rowTop = 36;

    for (size_t i = 0; i < FORECAST_DAYS; ++i) {
        const ForecastDay& day = forecast.days[i];
        if (!day.valid) {
            continue;
        }

        const auto rowY = static_cast<int16_t>(rowTop + (static_cast<int16_t>(i) * rowH));

        if (day.condition != _conditionCache[i]) {
            DrawUtils::weatherGlyph(6, rowY, 56, day.condition, BG_COLOR);
            _conditionCache[i] = day.condition;
        }

        DrawUtils::drawText(74, rowY + 6, LABELS[i], 1, MUTED, BG_COLOR, ALIGN_LEFT);

        const String range = String(WeatherClient::toDisplayTemp(day.maxC), 0) + "/" +
                             String(WeatherClient::toDisplayTemp(day.minC), 0) +
                             String(WeatherClient::temperatureUnit());
        DrawUtils::cachedText(74, rowY + 22, range, _rangeCache[i], 3, ACCENT, BG_COLOR, ALIGN_LEFT);
    }
}

/**
 * @brief Paint the simple weather clock chrome
 *
 * @return void
 */
void SimpleWeatherClockScreen::enter() {
    DisplayManager::getGfx()->fillScreen(BG_COLOR);

    _timeCache = "";
    _tempCache = "";
}

/**
 * @brief Repaint the simple weather clock
 *
 * @return void
 */
void SimpleWeatherClockScreen::tick() {
    const LocalTime local = TimeService::now();
    const TimeSettings& timeCfg = configManager.settings.time;
    const WeatherNow& weather = WeatherClient::current();

    DrawUtils::cachedText(PANEL_W / 2, 66, TimeService::formatTime(local, timeCfg.format12h, false), _timeCache, 6,
                          timeCfg.hour_color, BG_COLOR, ALIGN_CENTER);

    const String temp = weather.valid ? formatTemp(weather.tempC) : String("--");
    DrawUtils::cachedText(PANEL_W / 2, 140, temp, _tempCache, 4, ACCENT, BG_COLOR, ALIGN_CENTER);
}

// NOLINTEND(readability-magic-numbers)
