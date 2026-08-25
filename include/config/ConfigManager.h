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

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>
#include "config/SecureStorage.h"
#include "config/Settings.h"
#include <string>
#include <cstdint>
#include <SPI.h>

// LCD configuration defaults for SmallTV Ultra
static constexpr int16_t LCD_W = 240;
static constexpr int16_t LCD_H = 240;
static constexpr uint8_t LCD_ROTATION = 0;
static constexpr int8_t LCD_MOSI_GPIO = 13;
static constexpr int8_t LCD_SCK_GPIO = 14;
static constexpr int8_t LCD_DC_GPIO = 0;
static constexpr int8_t LCD_RST_GPIO = 2;
static constexpr uint8_t LCD_SPI_MODE = SPI_MODE3;
static constexpr uint32_t LCD_SPI_HZ = 40000000;
static constexpr int8_t LCD_BACKLIGHT_GPIO = 5;
static constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;

class ConfigManager {
   public:
    ConfigManager(const char* filename = "/config.json");
    bool load();
    bool save();
    void setWiFi(const char* newSsid, const char* newPassword);
    const char* getSSID() const;
    const char* getPassword() const;
    bool isWebAuthEnabled() const;
    void setWebAuthEnabled(bool enabled);
    const char* getWebUser() const;
    void setWebUser(const char* newUser);
    String getWebPassword() const;
    void setWebPassword(const char* newPassword);
    uint16_t getWebLifetimeSeconds() const;
    void setWebLifetimeSeconds(uint16_t seconds);
    uint8_t getLCDRotation() const;
    void setLCDRotation(uint8_t newRotation);
    uint32_t getLCDSpiHz() const;

   public:
    uint8_t getLCDRotationSafe() const { return lcd_rotation; }
    std::string ssid;
    std::string password;
    std::string filename;
    SecureStorage secure;
    uint8_t lcd_rotation = 0;
    std::string ntp_server;

    // Web access control. Auth is opt-in so the device behaves like the stock
    // firmware out of the box; the password itself lives in SecureStorage.
    bool web_auth_enabled = false;
    std::string web_user = "admin";

    // Seconds the web server stays up after boot; 0 keeps it up forever.
    uint16_t web_lifetime_s = 0;

    // Weather, clock, album and display preferences.
    Settings settings;

    const char* getNtpServer() const { return ntp_server.c_str(); }
    void setNtpServer(const char* s) {
        if (s) ntp_server = s;
    }
};

#endif  // CONFIG_MANAGER_H
