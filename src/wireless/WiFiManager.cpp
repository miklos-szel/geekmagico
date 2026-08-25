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

#include <ArduinoJson.h>
#include <Logger.h>

#include "wireless/WiFiManager.h"
#include "display/DisplayManager.h"

static constexpr int LOADING_BAR_TEXT_X = 20;
static constexpr int LOADING_BAR_TEXT_Y = 60;
static constexpr int LOADING_BAR_Y = 110;
static constexpr int LOADING_DELAY_MS = 1000;

/**
 * @brief Maximum number of attempts to connect to a wifi network
 */
static constexpr int MAX_CONNECTION_ATTEMPTS = 20;

/**
 * @brief Delay in milliseconds between wifi connection attempts
 */
static constexpr uint32_t CONNECTION_DELAY_MS = 500;

/**
 * @brief WifiManager constructor
 *
 * @param staSsid The SSID for the WiFi station mode
 * @param staPass The password for the WiFi station mode
 * @param apSsid The SSID for the WiFi access point mode
 * @param apPass The password for the WiFi access point mode
 */
WiFiManager::WiFiManager(const char* staSsid, const char* staPass, const char* apSsid, const char* apPass)
    : _staSsid(staSsid), _staPass(staPass), _apSsid(apSsid), _apPass(apPass) {}

auto WiFiManager::begin() -> void {
    if (!startStationMode()) {
        startAccessPointMode();
    }

    Logger::info("Wifi active", "WiFiManager");
    Logger::info(String("Mode : " + String(_apMode ? "AP" : "STA")).c_str(), "WiFiManager");
    Logger::info(String("SSID : " + String(_apMode ? _apSsid : _staSsid)).c_str(), "WiFiManager");
    Logger::info(String("IP   : " + getIP().toString()).c_str(), "WiFiManager");
}

/**
 * @brief Attempts to connect the device to a WiFi network in station mode
 *
 * @return true if the device successfully connects to the WiFi network false otherwise
 */
auto WiFiManager::startStationMode() -> bool {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_staSsid, _staPass);
    int attempts = 0;

    Logger::info("Connecting to WiFi...", "WiFiManager");

    while (WiFi.status() != WL_CONNECTED && attempts < MAX_CONNECTION_ATTEMPTS) {
        delay(CONNECTION_DELAY_MS);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _apMode = false;
        return true;
    }

    return false;
}

/**
 * @brief Begin an asynchronous WiFi scan
 *
 * Returns immediately. A blocking scan holds the radio for several seconds
 * and, while the device is serving its own access point, that drops the
 * browser's connection before the response can be sent.
 *
 * @return void
 */
auto WiFiManager::startScan() -> void {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        return;
    }

    // The STA radio has to be enabled to scan at all. In setup mode the
    // device runs WIFI_AP only (see startAccessPointMode()), and a scan
    // issued in that mode silently returns nothing on the ESP8266 core.
    // Switching to WIFI_AP_STA keeps the access point up - it does not by
    // itself connect the station or touch the current STA credentials.
    if (WiFi.getMode() == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    }

    Logger::info("Starting async WiFi scan", "WiFiManager");

    // async = true, show_hidden = false
    WiFi.scanNetworks(true, false);
}

/**
 * @brief Whether a scan is still running
 *
 * @return true while results are not yet available
 */
auto WiFiManager::scanInProgress() -> bool { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }

/**
 * @brief Collect the results of a finished scan
 *
 * Results are freed once read, so the next request starts a fresh scan, which
 * is what the Rescan button expects.
 *
 * @param out Array receiving one object per network
 *
 * @return Number of networks, or a negative WIFI_SCAN_* status
 */
auto WiFiManager::collectScanResults(JsonArray& out) -> int8_t {
    const int8_t found = WiFi.scanComplete();

    if (found < 0) {
        return found;
    }

    for (int8_t i = 0; i < found; ++i) {
        JsonObject obj = out.add<JsonObject>();

        obj["ssid"] = WiFi.SSID(i);                             // NOLINT(readability-misplaced-array-index)
        obj["rssi"] = static_cast<int>(WiFi.RSSI(i));           // NOLINT(readability-misplaced-array-index)
        obj["enc"] = static_cast<int>(WiFi.encryptionType(i));  // NOLINT(readability-misplaced-array-index)
    }

    WiFi.scanDelete();

    Logger::info((String("WiFi scan finished, ") + String(found) + " networks").c_str(), "WiFiManager");

    return found;
}


auto WiFiManager::connectToNetwork(const char* ssid, const char* pass, uint32_t timeoutMs) -> bool {
    Logger::info(String("Connecting to " + String(ssid)).c_str(), "WiFiManager");

    constexpr int total_steps = 2;
    int step = 0;

    DisplayManager::clearScreen();
    DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Wifi connecting...", 2, LCD_WHITE,
                                    LCD_BLACK, true);
    DisplayManager::drawLoadingBar(static_cast<float>(step) / static_cast<float>(total_steps), LOADING_BAR_Y);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(CONNECTION_DELAY_MS);
        DisplayManager::drawLoadingBar(static_cast<float>(step) / static_cast<float>(total_steps), LOADING_BAR_Y);
    }

    step++;

    if (WiFi.status() == WL_CONNECTED) {
        _apMode = false;

        Logger::info(String("Connected: " + WiFi.localIP().toString()).c_str(), "WiFiManager");
        DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Connected !", 2, LCD_WHITE, LCD_BLACK,
                                        true);
        DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y + ONE_LINE_SPACE,
                                        "IP: " + WiFi.localIP().toString(), 2, LCD_WHITE, LCD_BLACK, true);

        DisplayManager::drawLoadingBar(1.0F, LOADING_BAR_Y);

        return true;
    }

    DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Failed to connect!", 2, LCD_WHITE,
                                    LCD_BLACK, true);
    Logger::warn("Failed to connect to WiFi", "WiFiManager");

    DisplayManager::drawLoadingBar(1.0F, LOADING_BAR_Y);

    startAccessPointMode();

    return false;
}

auto WiFiManager::isConnected() -> bool { return WiFi.status() == WL_CONNECTED; }

auto WiFiManager::getConnectedSSID() -> String { return WiFi.SSID(); }

/**
 * @brief Starts the WiFi Access Point (AP) mode
 *
 * @return true Always returns true to indicate the AP mode was started
 */
auto WiFiManager::startAccessPointMode() -> bool {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apSsid, _apPass);

    _apMode = true;

    return true;
}

auto WiFiManager::isApMode() const -> bool { return _apMode; }

auto WiFiManager::getIP() const -> IPAddress { return _apMode ? WiFi.softAPIP() : WiFi.localIP(); }
