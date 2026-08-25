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
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <ESP8266HTTPUpdateServer.h>

#include <Logger.h>
#include "project_version.h"
#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include "display/DisplayManager.h"
#include "web/Webserver.h"
#include "web/Api.h"
#include "ntp/NTPClient.h"
#include "boot/RescueMode.h"
#include "dashboard/DashboardManager.h"
#include "weather/WeatherClient.h"
#include "screens/ScreenManager.h"
#include "time/TimeService.h"
#include "display/Backlight.h"
#include <array>

#ifndef METRICS_URL
#define METRICS_URL ""
#endif

ConfigManager configManager;
const char* AP_SSID = "GeekMagicO";
const char* AP_PASSWORD = "$str0ngPa$$w0rd";
WiFiManager* wifiManager = nullptr;
ESP8266HTTPUpdateServer httpUpdater;
// Do not change: this salt feeds SecureStorage key derivation (SecureStorage.cpp).
// Changing it makes every existing device unable to decrypt its stored WiFi
// credentials after an OTA, so it keeps its pre-fork value on purpose.
static constexpr const char* KV_SALT_STR = "GeekMagicOpenFirmwareIsAwesome";
static size_t initial_free_heap = 0;
static constexpr size_t FREE_BUF_SIZE = 32;
static constexpr size_t MSG_BUF_SIZE = 96;

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;
static constexpr int LOADING_BAR_TEXT_X = 50;
static constexpr int LOADING_BAR_TEXT_Y = 80;
static constexpr int LOADING_BAR_Y = 110;
static constexpr int LOADING_DELAY_MS = 1000;
static constexpr const char* METRICS_ENDPOINT = METRICS_URL;

Webserver* webserver = nullptr;
NTPClient* ntpClient = nullptr;

/**
 * @brief Formats bytes into a human-readable string
 *
 * @param value Size in bytes
 * @return Formatted string
 */
static void formatBytes(size_t value, char* outBuf, size_t outBufSize) {
    constexpr std::array<const char*, 5> UNITS = {"B", "KB", "MB", "GB", "TB"};
    constexpr double THRESHOLD = 1024.0;

    auto val = static_cast<double>(value);
    int unit = 0;
    while (val >= THRESHOLD && unit < static_cast<int>(UNITS.size()) - 1) {
        val /= THRESHOLD;
        ++unit;
    }

    if (unit == 0) {
        snprintf(outBuf, outBufSize, "%u %s", static_cast<unsigned int>(value), UNITS[unit]);
    } else {
        snprintf(outBuf, outBufSize, "%.1f %s", val, UNITS[unit]);
    }
}

/**
 * @brief Check whether LittleFS contains at least one entry
 *
 * @return true if filesystem root has any file/dir entry
 */
static auto littleFsHasEntries() -> bool {
    Dir dir = LittleFS.openDir("/");
    return dir.next();
}

/**
 * @brief Initializes the system
 *
 */
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(BOOT_DELAY_MS);
    Serial.println("");
    Logger::info(("GeekMagicO " + String(PROJECT_VER_STR)).c_str());

    constexpr int TOTAL_STEPS = 5;
    int step = 0;
    const bool littleFsMounted = LittleFS.begin();
    bool littleFsReadyForStatic = littleFsMounted;

    if (!littleFsMounted) {
        Logger::error("Failed to mount LittleFS");
        Logger::warn("LittleFS unavailable, static web UI disabled", "Global");
    } else if (!littleFsHasEntries()) {
        littleFsReadyForStatic = false;
        Logger::warn("LittleFS mounted but empty, static web UI disabled", "Global");
    }

    SecureStorage::setSalt(KV_SALT_STR);

    if (configManager.secure.begin()) {
        Logger::info("SecureStorage initialized successfully", "ConfigManager");
    }

    if (configManager.load()) {
        Logger::info("Configuration loaded successfully");
    }

    if (RescueMode::checkBootLoop()) {
        RescueMode::run();
        EspClass::wdtEnable(WDTO_2S);

        return;
    }

    step += 2;

    DisplayManager::begin();

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    step++;

    DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Starting...", 2, LCD_WHITE, LCD_BLACK,
                                    true);
    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
    step++;

    wifiManager = new WiFiManager(configManager.getSSID(), configManager.getPassword(), AP_SSID, AP_PASSWORD);
    wifiManager->begin();

    ntpClient = new NTPClient();
    ntpClient->begin();

    WeatherClient::begin();

    const DisplaySettings& displayCfg = configManager.settings.display;
    Backlight::configureNightMode(displayCfg.night_mode, displayCfg.night_start, displayCfg.night_end,
                                  displayCfg.night_brightness);
    Backlight::setDayBrightness(displayCfg.brightness);

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    step++;

    webserver = new Webserver();
    webserver->begin();

    initial_free_heap = ESP.getFreeHeap();  // NOLINT(readability-static-accessed-through-instance)

    DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);

    registerApiEndpoints(webserver);
    registerConfigApi(webserver);
    registerFilesApi(webserver);

    if (!littleFsReadyForStatic) {
        httpUpdater.setup(&webserver->raw(), "/legacyupdate");
        Logger::warn("Enabled legacy OTA route because LittleFS is unavailable or empty", "Global");
    } else {
        webserver->serveStaticC("/", "/web/index.html", "text/html");
        webserver->serveStaticC("/config.json", "/config.json", "application/json");
        webserver->registerGenericStaticFallback("/web", true);
    }

    DisplayManager::drawLoadingBar(1.0F, LOADING_BAR_Y);

    delay(LOADING_DELAY_MS);

    DisplayManager::drawStartup(wifiManager->getIP().toString());

    if (METRICS_ENDPOINT[0] != '\0' && WiFiManager::isConnected() && !wifiManager->isApMode()) {
        DashboardManager::begin(METRICS_ENDPOINT);
    } else {
        // Hand the panel to the theme engine; the metrics dashboard, when
        // compiled in, owns the screen instead.
        ScreenManager::begin();
    }

    // Optional hardening: shut the web server down a while after boot so a
    // device left running as a photo frame stops exposing a network surface.
    // Deliberately not armed in AP mode, where closing the server would lock
    // the user out before they could ever configure WiFi.
    if (wifiManager->isApMode()) {
        if (configManager.getWebLifetimeSeconds() > 0) {
            Logger::info("Web lifetime window not armed while in setup (AP) mode", "Global");
        }
    } else {
        webserver->armLifetimeWindow(configManager.getWebLifetimeSeconds());
    }

    // enable watchdog before going to loop()
    // 2 seconds should be way more than the main loop needs to do stuff
    EspClass::wdtEnable(WDTO_2S);
}

void loop() {
    if (RescueMode::isActive()) {
        RescueMode::loop();
        return;
    }

    static bool bootStableMarked = false;
    if (!bootStableMarked && millis() >= BOOT_STABLE_MS) {
        RescueMode::markBootStable();
        bootStableMarked = true;
    }

    if (webserver != nullptr) {
        webserver->handleClient();
    }

    if (ntpClient != nullptr) {
        ntpClient->loop();
    }

    WeatherClient::loop();

    // Cheap: only touches the backlight pin on a day/night transition.
    Backlight::applySchedule(TimeService::now().minutesOfDay);

    ScreenManager::loop();

    DisplayManager::update();

    if (METRICS_ENDPOINT[0] != '\0' && wifiManager != nullptr && WiFiManager::isConnected() &&
        !wifiManager->isApMode()) {
        DashboardManager::update();
    }

    static unsigned long last_free_heap_log = 0;
    static constexpr unsigned long FREE_HEAP_LOG_INTERVAL_MS = 10000UL;
    unsigned long now = millis();

    if (now - last_free_heap_log >= FREE_HEAP_LOG_INTERVAL_MS) {
        last_free_heap_log = now;
        char freeBuf[FREE_BUF_SIZE];
        char initBuf[FREE_BUF_SIZE];
        char msgBuf[MSG_BUF_SIZE];

        formatBytes(ESP.getFreeHeap(), freeBuf,  // NOLINT(readability-static-accessed-through-instance)
                    sizeof(freeBuf));
        formatBytes(initial_free_heap, initBuf, sizeof(initBuf));

        snprintf(msgBuf, sizeof(msgBuf), "Free heap: %s (initial: %s)", freeBuf, initBuf);
        Logger::info(msgBuf);
    }

    EspClass::wdtFeed();  // kick watchdog
}
