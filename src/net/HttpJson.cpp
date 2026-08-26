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
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include "net/HttpJson.h"
#include "wireless/WiFiManager.h"

namespace {

constexpr uint16_t HTTP_TIMEOUT_MS = 8000;

}  // namespace

/**
 * @brief Run an HTTP GET and parse the response through a filter
 *
 * @param url The URL to fetch
 * @param filter Filter describing the fields to keep
 * @param doc Document receiving the filtered result
 * @param status Receives a short description of what went wrong, or "ok"
 *
 * @return true when the request and parse both succeeded
 */
auto HttpJson::fetchFiltered(const String& url, JsonDocument& filter, JsonDocument& doc, String& status) -> bool {
    if (!WiFiManager::isConnected()) {
        status = "network unavailable";
        return false;
    }

    WiFiClient client;
    HTTPClient http;

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    http.useHTTP10(true);

    if (!http.begin(client, url)) {
        status = "request setup failed";
        return false;
    }

    EspClass::wdtFeed();

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        status = String("HTTP ") + String(code);
        http.end();

        return false;
    }

    EspClass::wdtFeed();

    const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

    http.end();
    EspClass::wdtFeed();
    yield();

    if (error) {
        status = String("parse error: ") + error.c_str();
        return false;
    }

    status = "ok";

    return true;
}
