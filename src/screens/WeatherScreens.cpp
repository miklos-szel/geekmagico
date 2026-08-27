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
constexpr int16_t ICON_BOX = 80;

// Packed into the icon cache alongside the condition. Conditions top out at
// WX_THUNDER (9), so this never collides with the 0xFF repaint sentinel.
constexpr uint8_t NIGHT_BIT = 0x80;

// Weather clock geometry. The icon and the temperature share a row and are
// centred on the same axis (icon 80..160, temperature 100..140, both centred
// on y=120). The condition and detail lines get the full panel width because
// they do not fit beside the icon: "Partly cloudy" is 156px and
// "H100%  100.0km/h" is 192px, so right-aligning either one to x=232 puts its
// left edge inside the icon box.
constexpr int16_t ICON_X = 8;
constexpr int16_t ICON_Y = 80;
constexpr int16_t TIME_Y = 8;
constexpr int16_t DATE_Y = 54;
constexpr int16_t TEMP_Y = 100;
constexpr int16_t CONDITION_Y = 172;
constexpr int16_t DETAIL_Y = 196;

// The fallback message shares the condition row rather than sitting in the
// icon box, which the weather icon owns.
constexpr int16_t NO_DATA_Y = CONDITION_Y;
constexpr int16_t TEXT_RIGHT = 232;
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
 * @brief Paint the weather clock chrome and start the icon animation if set
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
    _iconCondition = 0xFF;
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

    DrawUtils::cachedText(PANEL_W / 2, TIME_Y, TimeService::formatTime(local, timeCfg.format12h, false), _timeCache, 5,
                          timeCfg.hour_color, BG_COLOR, ALIGN_CENTER);

    DrawUtils::cachedText(PANEL_W / 2, DATE_Y, TimeService::formatDate(local, timeCfg.date_format.c_str()), _dateCache,
                          2, MUTED, BG_COLOR, ALIGN_CENTER);

    if (!weather.valid) {
        // Clear whatever was painted the last time weather was valid so it
        // doesn't linger behind the fallback message.
        DrawUtils::cachedText(TEXT_RIGHT, TEMP_Y, "", _tempCache, 5, ACCENT, BG_COLOR, ALIGN_RIGHT);
        DrawUtils::cachedText(PANEL_W / 2, CONDITION_Y, "", _conditionCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
        DrawUtils::cachedText(PANEL_W / 2, DETAIL_Y, "", _detailCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);

        // The icon claims something about the weather, so it goes when the
        // reading does.
        if (_iconCondition != 0xFF) {
            DisplayManager::getGfx()->fillRect(ICON_X, ICON_Y, ICON_BOX, ICON_BOX, BG_COLOR);
            _iconCondition = 0xFF;
        }

        DrawUtils::cachedText(PANEL_W / 2, NO_DATA_Y, "No weather data", _noDataCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
        return;
    }

    if (_noDataCache.length() != 0) {
        DrawUtils::cachedText(PANEL_W / 2, NO_DATA_Y, "", _noDataCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
    }

    // The cache key carries the night bit, or the icon would never swap after
    // dark; it is the normalised flag, so conditions with no night artwork do
    // not repaint an identical image at dusk and dawn.
    const bool night = DrawUtils::weatherIconIsNight(weather.condition, weather.isNight);
    const auto iconKey = static_cast<uint8_t>(static_cast<uint8_t>(weather.condition) | (night ? NIGHT_BIT : 0U));

    if (iconKey != _iconCondition) {
        // weatherGlyph() switches on the raw condition, so it must never see
        // the packed key -- every case would miss and fall to the unknown ring.
        if (!DrawUtils::weatherIcon(ICON_X, ICON_Y, ICON_BOX, weather.condition, weather.isNight)) {
            DrawUtils::weatherGlyph(ICON_X, ICON_Y, ICON_BOX, weather.condition, BG_COLOR);
        }
        _iconCondition = iconKey;
    }

    DrawUtils::cachedText(TEXT_RIGHT, TEMP_Y, formatTemp(weather.tempC), _tempCache, 5, ACCENT, BG_COLOR, ALIGN_RIGHT);
    DrawUtils::cachedText(PANEL_W / 2, CONDITION_Y, WeatherClient::conditionLabel(weather.condition), _conditionCache, 2,
                          MUTED, BG_COLOR, ALIGN_CENTER);

    const String detail = String("H") + String(weather.humidity) + "%  " +
                          String(WeatherClient::toDisplayWind(weather.windMs), 1) + WeatherClient::windUnit();
    DrawUtils::cachedText(PANEL_W / 2, DETAIL_Y, detail, _detailCache, 2, MUTED, BG_COLOR, ALIGN_CENTER);
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

        // A forecast covers a whole day, so it always uses the day artwork.
        if (day.condition != _conditionCache[i]) {
            if (!DrawUtils::weatherIcon(6, rowY, 56, day.condition, false)) {
                DrawUtils::weatherGlyph(6, rowY, 56, day.condition, BG_COLOR);
            }
            _conditionCache[i] = day.condition;
        }

        // +8/+24 rather than +6/+22: the label and range together span 40px,
        // which centres on the 56px icon's axis only from here.
        DrawUtils::drawText(74, rowY + 8, LABELS[i], 1, MUTED, BG_COLOR, ALIGN_LEFT);

        const String range = String(WeatherClient::toDisplayTemp(day.maxC), 0) + "/" +
                             String(WeatherClient::toDisplayTemp(day.minC), 0) +
                             String(WeatherClient::temperatureUnit());
        DrawUtils::cachedText(74, rowY + 24, range, _rangeCache[i], 3, ACCENT, BG_COLOR, ALIGN_LEFT);
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
