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
#include <LittleFS.h>
#include <Logger.h>

#include "web/Api.h"
#include "web/Webserver.h"
#include "config/ConfigManager.h"
#include "config/Settings.h"
#include "display/Backlight.h"
#include "display/DisplayManager.h"
#include "screens/ScreenManager.h"
#include "storage/FileStore.h"
#include "time/TimeService.h"
#include "weather/WeatherClient.h"
#include "wireless/WiFiManager.h"
#include "project_version.h"

extern ConfigManager configManager;
extern WiFiManager* wifiManager;

// ArduinoJson's obj["key"] accessors trip readability-misplaced-array-index.
// NOLINTBEGIN(readability-misplaced-array-index)

namespace {

constexpr const char* TAG = "API";
constexpr int REBOOT_DELAY_MS = 1000;
constexpr uint8_t MAX_ROTATION = 7;

/**
 * @brief Persist settings and report failure to the client
 */
auto persist(Webserver* webserver) -> bool {
    if (configManager.save()) {
        return true;
    }

    sendStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "error", "Failed to save config");

    return false;
}

/**
 * @brief Copy a string field into a std::string when present
 */
void assignString(JsonDocument& doc, const char* key, std::string& target) {
    if (doc[key].is<const char*>()) {
        const char* value = doc[key].as<const char*>();
        if (value != nullptr) {
            target = value;
        }
    }
}

// ---------------------------------------------------------------- weather

void weatherConfigGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const WeatherSettings& cfg = configManager.settings.weather;

    JsonDocument doc;
    doc["city"] = configManager.settings.city.c_str();
    doc["interval_min"] = cfg.interval_min;
    doc["wind"] = cfg.wind.c_str();
    doc["temp"] = cfg.temp.c_str();
    doc["pressure"] = cfg.pressure.c_str();
    doc["gif"] = cfg.gif.c_str();

    // Keys are write-only: report whether one is set, never echo it back.
    doc["api_key_set"] = !cfg.api_key.empty();
    doc["forecast_key_set"] = !cfg.forecast_key.empty();

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void weatherConfigSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    WeatherSettings& cfg = configManager.settings.weather;

    assignString(doc, "city", configManager.settings.city);
    assignString(doc, "wind", cfg.wind);
    assignString(doc, "temp", cfg.temp);
    assignString(doc, "pressure", cfg.pressure);
    assignString(doc, "gif", cfg.gif);
    assignString(doc, "api_key", cfg.api_key);
    assignString(doc, "forecast_key", cfg.forecast_key);

    if (doc["interval_min"].is<uint16_t>()) {
        cfg.interval_min = doc["interval_min"].as<uint16_t>();
    }

    if (!persist(webserver)) {
        return;
    }

    // Apply straight away so the user sees the effect without a reboot.
    WeatherClient::refreshNow();
    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Weather settings saved");
}

void weatherCurrentGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const WeatherNow& weather = WeatherClient::current();

    JsonDocument doc;
    doc["valid"] = weather.valid;
    doc["status"] = WeatherClient::lastStatus();
    doc["provider"] = (WeatherClient::providerInUse() == WX_PROVIDER_OWM) ? "openweathermap"
                      : (WeatherClient::providerInUse() == WX_PROVIDER_KEYLESS) ? "keyless"
                                                                                : "none";

    if (weather.valid) {
        doc["temp"] = WeatherClient::toDisplayTemp(weather.tempC);
        doc["feels_like"] = WeatherClient::toDisplayTemp(weather.feelsLikeC);
        doc["wind"] = WeatherClient::toDisplayWind(weather.windMs);
        doc["pressure"] = WeatherClient::toDisplayPressure(weather.pressureHpa);
        doc["humidity"] = weather.humidity;
        doc["condition"] = WeatherClient::conditionLabel(weather.condition);
        doc["description"] = weather.description.data();
        doc["temp_unit"] = WeatherClient::temperatureUnit();
        doc["wind_unit"] = WeatherClient::windUnit();
        doc["pressure_unit"] = WeatherClient::pressureUnit();
    }

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void weatherRefresh(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const bool refreshed = WeatherClient::refreshNow();

    sendStatus(webserver, HTTP_CODE_OK, refreshed ? "ok" : "error", WeatherClient::lastStatus().c_str());
}

// ------------------------------------------------------------------- time

void timeConfigGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const TimeSettings& cfg = configManager.settings.time;

    JsonDocument doc;
    doc["tz_mode"] = cfg.tz_mode.c_str();
    doc["utc_offset_min"] = cfg.utc_offset_min;
    doc["hour_color"] = rgb565ToHex(cfg.hour_color);
    doc["minute_color"] = rgb565ToHex(cfg.minute_color);
    doc["second_color"] = rgb565ToHex(cfg.second_color);
    doc["format12h"] = cfg.format12h;
    doc["date_format"] = cfg.date_format.c_str();
    doc["colon_blink"] = cfg.colon_blink;
    doc["font"] = cfg.font;
    doc["ntp_server"] = configManager.getNtpServer();

    // The keyless weather provider carries no UTC offset, so tell the UI when
    // "auto" cannot actually resolve one.
    doc["auto_offset_available"] = WeatherClient::current().tzOffsetKnown;
    doc["effective_offset_min"] = TimeService::offsetMinutes();

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void timeConfigSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    TimeSettings& cfg = configManager.settings.time;

    assignString(doc, "tz_mode", cfg.tz_mode);
    assignString(doc, "date_format", cfg.date_format);

    if (doc["utc_offset_min"].is<int16_t>()) {
        cfg.utc_offset_min = doc["utc_offset_min"].as<int16_t>();
    }
    if (doc["hour_color"].is<const char*>()) {
        cfg.hour_color = hexToRgb565(doc["hour_color"].as<const char*>(), cfg.hour_color);
    }
    if (doc["minute_color"].is<const char*>()) {
        cfg.minute_color = hexToRgb565(doc["minute_color"].as<const char*>(), cfg.minute_color);
    }
    if (doc["second_color"].is<const char*>()) {
        cfg.second_color = hexToRgb565(doc["second_color"].as<const char*>(), cfg.second_color);
    }
    if (doc["format12h"].is<bool>()) {
        cfg.format12h = doc["format12h"].as<bool>();
    }
    if (doc["colon_blink"].is<bool>()) {
        cfg.colon_blink = doc["colon_blink"].as<bool>();
    }
    if (doc["font"].is<uint8_t>()) {
        cfg.font = doc["font"].as<uint8_t>();
    }
    if (doc["ntp_server"].is<const char*>()) {
        configManager.setNtpServer(doc["ntp_server"].as<const char*>());
    }

    if (!persist(webserver)) {
        return;
    }

    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Time settings saved");
}

// --------------------------------------------------------------- pictures

void picturesConfigGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const PictureSettings& cfg = configManager.settings.pictures;

    JsonDocument doc;
    doc["auto_display"] = cfg.auto_display;
    doc["shuffle"] = cfg.shuffle;
    doc["interval_s"] = cfg.interval_s;
    doc["current"] = cfg.current.c_str();

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void picturesConfigSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    PictureSettings& cfg = configManager.settings.pictures;

    if (doc["auto_display"].is<bool>()) {
        cfg.auto_display = doc["auto_display"].as<bool>();
    }
    if (doc["shuffle"].is<bool>()) {
        cfg.shuffle = doc["shuffle"].as<bool>();
    }
    if (doc["interval_s"].is<uint16_t>()) {
        cfg.interval_s = doc["interval_s"].as<uint16_t>();
    }
    assignString(doc, "current", cfg.current);

    if (!persist(webserver)) {
        return;
    }

    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Picture settings saved");
}

// ---------------------------------------------------------------- display

void displayConfigGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const DisplaySettings& cfg = configManager.settings.display;

    JsonDocument doc;
    doc["theme"] = cfg.theme;
    doc["theme_name"] = ScreenManager::themeName(cfg.theme);
    doc["auto_switch"] = cfg.auto_switch;
    doc["auto_switch_interval_s"] = cfg.auto_switch_interval_s;
    doc["auto_switch_mask"] = cfg.auto_switch_mask;
    doc["brightness"] = cfg.brightness;
    doc["night_mode"] = cfg.night_mode;
    doc["night_start"] = minutesToHhmm(cfg.night_start);
    doc["night_end"] = minutesToHhmm(cfg.night_end);
    doc["night_brightness"] = cfg.night_brightness;
    doc["rotation"] = configManager.getLCDRotationSafe();
    doc["night_active"] = Backlight::isNightActive();

    JsonArray themes = doc["themes"].to<JsonArray>();
    for (uint8_t theme = 0; theme < THEME_COUNT; ++theme) {
        themes.add(ScreenManager::themeName(theme));
    }

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void displayConfigSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    DisplaySettings& cfg = configManager.settings.display;
    bool rotationChanged = false;

    if (doc["theme"].is<uint8_t>()) {
        const uint8_t theme = doc["theme"].as<uint8_t>();
        if (theme >= THEME_COUNT) {
            sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "theme out of range");
            return;
        }
        cfg.theme = theme;
    }
    if (doc["auto_switch"].is<bool>()) {
        cfg.auto_switch = doc["auto_switch"].as<bool>();
    }
    if (doc["auto_switch_interval_s"].is<uint16_t>()) {
        cfg.auto_switch_interval_s = doc["auto_switch_interval_s"].as<uint16_t>();
    }
    if (doc["auto_switch_mask"].is<uint8_t>()) {
        cfg.auto_switch_mask = doc["auto_switch_mask"].as<uint8_t>();
    }
    if (doc["brightness"].is<uint8_t>()) {
        cfg.brightness = doc["brightness"].as<uint8_t>();
    }
    if (doc["night_mode"].is<bool>()) {
        cfg.night_mode = doc["night_mode"].as<bool>();
    }
    if (doc["night_start"].is<const char*>()) {
        cfg.night_start = hhmmToMinutes(doc["night_start"].as<const char*>(), cfg.night_start);
    }
    if (doc["night_end"].is<const char*>()) {
        cfg.night_end = hhmmToMinutes(doc["night_end"].as<const char*>(), cfg.night_end);
    }
    if (doc["night_brightness"].is<uint8_t>()) {
        cfg.night_brightness = doc["night_brightness"].as<uint8_t>();
    }
    if (doc["rotation"].is<uint8_t>()) {
        const uint8_t rotation = doc["rotation"].as<uint8_t>();
        if (rotation > MAX_ROTATION) {
            sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "rotation must be 0-7");
            return;
        }
        configManager.setLCDRotation(rotation);
        rotationChanged = true;
    }

    if (!persist(webserver)) {
        return;
    }

    Backlight::configureNightMode(cfg.night_mode, cfg.night_start, cfg.night_end, cfg.night_brightness);
    Backlight::setDayBrightness(cfg.brightness);

    if (rotationChanged) {
        DisplayManager::getGfx()->setRotation(configManager.getLCDRotationSafe());
    }

    ScreenManager::showTheme(cfg.theme);
    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Display settings saved");
}

// -------------------------------------------------------------------- web

void webConfigGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["auth_enabled"] = configManager.isWebAuthEnabled();
    doc["user"] = configManager.getWebUser();
    doc["password_set"] = configManager.getWebPassword().length() > 0;
    doc["lifetime_s"] = configManager.getWebLifetimeSeconds();
    doc["lifetime_remaining_s"] = webserver->secondsRemaining();
    doc["ap_mode"] = (wifiManager != nullptr) && wifiManager->isApMode();

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void webConfigSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    if (doc["user"].is<const char*>()) {
        configManager.setWebUser(doc["user"].as<const char*>());
    }
    if (doc["password"].is<const char*>()) {
        configManager.setWebPassword(doc["password"].as<const char*>());
    }
    if (doc["lifetime_s"].is<uint16_t>()) {
        configManager.setWebLifetimeSeconds(doc["lifetime_s"].as<uint16_t>());
    }

    if (doc["auth_enabled"].is<bool>()) {
        const bool enable = doc["auth_enabled"].as<bool>();

        // Refuse to arm auth with no password: that would either lock the user
        // out or silently fail open. Make them set one first.
        if (enable && configManager.getWebPassword().length() == 0) {
            sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "Set a password before enabling auth");
            return;
        }

        configManager.setWebAuthEnabled(enable);
    }

    if (!persist(webserver)) {
        return;
    }

    Logger::info("Web security settings updated", TAG);

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Web settings saved, lifetime applies on next boot");
}

// ----------------------------------------------------------------- system

void systemInfoGet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    JsonDocument doc;
    doc["model"] = "SmallTV-Ultra";
    doc["firmware"] = "GeekMagicO";
    doc["version"] = PROJECT_VER_STR;
    doc["free_heap"] = EspClass::getFreeHeap();
    doc["chip_id"] = EspClass::getChipId();
    doc["uptime_s"] = millis() / 1000UL;
    doc["fs_total"] = FileStore::totalBytes();
    doc["fs_used"] = FileStore::usedBytes();
    doc["fs_free"] = FileStore::freeBytes();
    doc["theme"] = ScreenManager::activeTheme();
    doc["theme_name"] = ScreenManager::activeThemeName();

    if (wifiManager != nullptr) {
        doc["ip"] = wifiManager->getIP().toString();
        doc["ap_mode"] = wifiManager->isApMode();
        doc["ssid"] = WiFiManager::isConnected() ? WiFiManager::getConnectedSSID() : "";
    }

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void systemFactoryReset(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    // Matches the original firmware: settings are cleared, uploaded files are
    // left alone.
    bool removed = true;
    if (LittleFS.exists("/config.json")) {
        removed = LittleFS.remove("/config.json");
    }

    configManager.secure.remove("wifi_ssid");
    configManager.secure.remove("wifi_password");
    configManager.secure.remove("web_password");

    Logger::warn("Factory reset requested via API", TAG);

    sendStatus(webserver, removed ? HTTP_CODE_OK : HTTP_CODE_INTERNAL_ERROR, removed ? "ok" : "error",
               removed ? "Settings cleared, rebooting" : "Failed to clear settings");

    if (removed) {
        delay(REBOOT_DELAY_MS);
        EspClass::restart();
    }
}

}  // namespace

/**
 * @brief Register the settings endpoints backing the five-tab web UI
 *
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void registerConfigApi(Webserver* webserver) {
    // @openapi {get} /weather/config version=v1 group=Weather summary="Get weather settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/weather/config", HTTP_GET, [webserver]() { weatherConfigGet(webserver); });

    // @openapi {post} /weather/config version=v1 group=Weather summary="Set weather settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=city:string,api_key:string,interval_min:integer
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/weather/config", HTTP_POST, [webserver]() { weatherConfigSet(webserver); });

    // @openapi {get} /weather/current version=v1 group=Weather summary="Get last weather reading" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/weather/current", HTTP_GET, [webserver]() { weatherCurrentGet(webserver); });

    // @openapi {post} /weather/refresh version=v1 group=Weather summary="Force a weather refresh" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/weather/refresh", HTTP_POST, [webserver]() { weatherRefresh(webserver); });

    // @openapi {get} /time/config version=v1 group=Time summary="Get clock settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/time/config", HTTP_GET, [webserver]() { timeConfigGet(webserver); });

    // @openapi {post} /time/config version=v1 group=Time summary="Set clock settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=tz_mode:string,format12h:boolean,date_format:string
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/time/config", HTTP_POST, [webserver]() { timeConfigSet(webserver); });

    // @openapi {get} /pictures/config version=v1 group=Pictures summary="Get slideshow settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/pictures/config", HTTP_GET, [webserver]() { picturesConfigGet(webserver); });

    // @openapi {post} /pictures/config version=v1 group=Pictures summary="Set slideshow settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=auto_display:boolean,interval_s:integer
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/pictures/config", HTTP_POST, [webserver]() { picturesConfigSet(webserver); });

    // @openapi {get} /display/config version=v1 group=Display summary="Get display settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/display/config", HTTP_GET, [webserver]() { displayConfigGet(webserver); });

    // @openapi {post} /display/config version=v1 group=Display summary="Set display settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=theme:integer,brightness:integer,night_mode:boolean
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/display/config", HTTP_POST, [webserver]() { displayConfigSet(webserver); });

    // @openapi {get} /web/config version=v1 group=System summary="Get web security settings" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/web/config", HTTP_GET, [webserver]() { webConfigGet(webserver); });

    // @openapi {post} /web/config version=v1 group=System summary="Set web security settings" requiresAuth=true
    // requestBody=application/json requestBodySchema=auth_enabled:boolean,password:string,lifetime_s:integer
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/web/config", HTTP_POST, [webserver]() { webConfigSet(webserver); });

    // @openapi {get} /system/info version=v1 group=System summary="Get device information" requiresAuth=true
    // responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/system/info", HTTP_GET, [webserver]() { systemInfoGet(webserver); });

    // @openapi {post} /system/factory-reset version=v1 group=System summary="Clear settings, keep files"
    // requiresAuth=true responses=200:application/json,401:application/json
    webserver->raw().on("/api/v1/system/factory-reset", HTTP_POST, [webserver]() { systemFactoryReset(webserver); });

    Logger::info("Config API registered", TAG);
}

// NOLINTEND(readability-misplaced-array-index)
