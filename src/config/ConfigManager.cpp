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
#include <LittleFS.h>

#include <Logger.h>
#include "config/ConfigManager.h"
#include "config/SecureStorage.h"
#include "config/Settings.h"

ConfigManager::ConfigManager(const char* filename) : filename(filename), secure() {}

/**
 * @brief Loads the configuration from a file stored in SPIFFS
 *
 * @return true if the configuration was successfully loaded and parsed false otherwise
 */
auto ConfigManager::load() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");
        return false;
    }

    File file = LittleFS.open(filename.c_str(), "r");
    if (!file) {
        Logger::error("Failed to open config file", "ConfigManager");
        return false;
    }

    size_t size = file.size();
    if (size == 0) {
        Logger::warn("Config file is empty", "ConfigManager");
        file.close();
        return false;
    }

    std::unique_ptr<char[]> buf(new char[size + 1]);
    file.readBytes(buf.get(), size);
    buf[size] = '\0';
    file.close();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf.get());
    if (error) {
        Logger::error(("Failed to parse config file : " + String(error.c_str())).c_str(), "ConfigManager");
        return false;
    }

    String ssid = doc["wifi_ssid"] | "";
    String password = doc["wifi_password"] | "";
    String ntp_server_cfg = doc["ntp_server"] | "";

    this->lcd_rotation = doc["lcd_rotation"] | lcd_rotation;

    JsonObjectConst web = doc["web"];
    if (web["auth_enabled"].is<bool>()) {                          // NOLINT(readability-misplaced-array-index)
        this->web_auth_enabled = web["auth_enabled"].as<bool>();   // NOLINT(readability-misplaced-array-index)
    }
    if (web["user"].is<const char*>()) {                           // NOLINT(readability-misplaced-array-index)
        this->web_user = web["user"].as<const char*>();            // NOLINT(readability-misplaced-array-index)
    }
    if (web["lifetime_s"].is<uint16_t>()) {                        // NOLINT(readability-misplaced-array-index)
        this->web_lifetime_s = web["lifetime_s"].as<uint16_t>();   // NOLINT(readability-misplaced-array-index)
    }

    settingsFromJson(doc.as<JsonObjectConst>(), this->settings);

    // The bearer API token was removed in GeekMagicO. Purge any value left
    // behind by an older build so upgraded devices stop carrying the secret.
    if (secure.get("api_token", "").length() != 0) {
        secure.remove("api_token");
        Logger::info("Removed obsolete api_token from SecureStorage", "ConfigManager");
    }

    String nvs_ssid = secure.get("wifi_ssid", "");
    String nvs_password = secure.get("wifi_password", "");

    if ((ssid.length() != 0 && nvs_ssid.length() == 0) || (password.length() != 0 && nvs_password.length() == 0)) {
        secure.put("wifi_ssid", ssid.c_str());
        secure.put("wifi_password", password.c_str());

        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();

        if (ntp_server_cfg.length() != 0) {
            this->ntp_server = ntp_server_cfg.c_str();
        }

        // Ensure we delete the wifi credentials from the json config after migrating
        ConfigManager::save();

        Logger::info("WiFi credentials migrated to SecureStorage", "ConfigManager");
    } else {
        this->ssid = secure.get("wifi_ssid").c_str();
        this->password = secure.get("wifi_password").c_str();
    }

    return true;
}

/**
 * @brief Retrieves the current Wi-Fi SSID
 *
 * @return The SSID as a c style string
 */
auto ConfigManager::getSSID() const -> const char* { return ssid.c_str(); }

/**
 * @brief Retrieves the current Wi-Fi password
 *
 * @return The password as a c style string
 */
auto ConfigManager::getPassword() const -> const char* { return password.c_str(); }

/**
 * @brief Whether HTTP Basic auth is required for the web UI and API
 *
 * @return true when auth is enabled
 */
auto ConfigManager::isWebAuthEnabled() const -> bool { return web_auth_enabled; }

/**
 * @brief Enable or disable HTTP Basic auth
 *
 * @param enabled Whether auth should be required
 *
 * @return void
 */
auto ConfigManager::setWebAuthEnabled(bool enabled) -> void { web_auth_enabled = enabled; }

/**
 * @brief Retrieves the web auth username
 *
 * @return The username as a c style string
 */
auto ConfigManager::getWebUser() const -> const char* { return web_user.c_str(); }

/**
 * @brief Set the web auth username in memory
 *
 * @param newUser The username
 *
 * @return void
 */
// NOLINTBEGIN(readability-convert-member-functions-to-static)
auto ConfigManager::setWebUser(const char* newUser) -> void {
    if (newUser != nullptr && newUser[0] != '\0') {
        web_user = newUser;
    }
}

/**
 * @brief Retrieves the web auth password from SecureStorage
 *
 * The password is never held in config.json nor cached in RAM.
 *
 * @return The password, empty when unset
 */
auto ConfigManager::getWebPassword() const -> String {
    return const_cast<SecureStorage&>(secure).get("web_password", "");
}

/**
 * @brief Persist the web auth password to SecureStorage
 *
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setWebPassword(const char* newPassword) -> void {
    if (newPassword == nullptr) {
        return;
    }

    if (newPassword[0] == '\0') {
        secure.remove("web_password");
        return;
    }

    secure.put("web_password", newPassword);
}
// NOLINTEND(readability-convert-member-functions-to-static)

/**
 * @brief Seconds the web server stays reachable after boot (0 = always)
 *
 * @return The configured lifetime in seconds
 */
auto ConfigManager::getWebLifetimeSeconds() const -> uint16_t { return web_lifetime_s; }

/**
 * @brief Set the web server lifetime window in memory
 *
 * @param seconds Lifetime in seconds, 0 to keep the server up forever
 *
 * @return void
 */
auto ConfigManager::setWebLifetimeSeconds(uint16_t seconds) -> void { web_lifetime_s = seconds; }

/**
 * @brief Retrieves the LCD rotation setting
 *
 * @return The rotation of the LCD
 */
auto ConfigManager::getLCDRotation() const -> uint8_t { return lcd_rotation; }

/**
 * @brief Set LCD rotation in memory
 *
 * @param newRotation Rotation value in range [0, 7]
 *
 * @return void
 */
auto ConfigManager::setLCDRotation(uint8_t newRotation) -> void { lcd_rotation = newRotation; }

/**
 * @brief Set WiFi credentials in memory
 * @param newSsid The SSID
 * @param newPassword The password
 *
 * @return void
 */
auto ConfigManager::setWiFi(const char* newSsid, const char* newPassword) -> void {
    if (newSsid != nullptr) {
        ssid = newSsid;
    }
    if (newPassword != nullptr) {
        password = newPassword;
    }
}

/**
 * @brief Save the current configuration to the file
 *
 * @param clearWifiCreds If true wifi credentials will be cleared from json config
 *
 * @return true if the configuration was successfully saved false otherwise
 */
auto ConfigManager::save() -> bool {
    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS", "ConfigManager");

        return false;
    }

    File file = LittleFS.open(filename.c_str(), "w");

    if (!file) {
        Logger::error("Failed to open config file for writing", "ConfigManager");

        return false;
    }

    JsonDocument doc;

    secure.put("wifi_ssid", this->getSSID());
    secure.put("wifi_password", this->getPassword());

    // Claim the root first: to<JsonObject>() clears the document, so anything
    // written before this call would be discarded.
    JsonObject root = doc.to<JsonObject>();
    settingsToJson(this->settings, root);

    root["lcd_rotation"] = lcd_rotation;  // NOLINT(readability-misplaced-array-index)
    if (!this->ntp_server.empty()) {
        root["ntp_server"] = this->ntp_server.c_str();  // NOLINT(readability-misplaced-array-index)
    }

    JsonObject web = root["web"].to<JsonObject>();  // NOLINT(readability-misplaced-array-index)
    web["auth_enabled"] = web_auth_enabled;   // NOLINT(readability-misplaced-array-index)
    web["user"] = web_user.c_str();           // NOLINT(readability-misplaced-array-index)
    web["lifetime_s"] = web_lifetime_s;       // NOLINT(readability-misplaced-array-index)

    if (serializeJson(doc, file) == 0) {
        Logger::error("Failed to write config file", "ConfigManager");
        file.close();

        return false;
    }

    file.close();
    Logger::info("Configuration saved", "ConfigManager");

    return true;
}
