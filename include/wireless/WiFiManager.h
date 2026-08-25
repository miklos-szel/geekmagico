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

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>

class WiFiManager {
   public:
    WiFiManager(const char* staSsid, const char* staPass, const char* apSsid, const char* apPass);
    void begin();
    bool startStationMode();
    bool startAccessPointMode();
    bool isApMode() const;
    IPAddress getIP() const;
    // Asynchronous scan. A blocking scan makes the radio hop channels for
    // seconds, which drops any client connected to the device's own access
    // point mid-request - so the browser never sees the response.
    static auto startScan() -> void;
    static auto scanInProgress() -> bool;
    static auto collectScanResults(JsonArray& out) -> int8_t;
    bool connectToNetwork(const char* ssid, const char* pass, uint32_t timeoutMs = 10000);
    static bool isConnected();
    static String getConnectedSSID();

   private:
    const char* _staSsid;
    const char* _staPass;
    const char* _apSsid;
    const char* _apPass;
    bool _apMode = false;
};

#endif  // WIFI_MANAGER_H
