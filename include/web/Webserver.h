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

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <functional>

/**
 * @brief HTTP status code 200
 */
static int constexpr HTTP_CODE_OK = 200;

/**
 * @brief HTTP status code 400
 */
static int constexpr HTTP_CODE_BAD_REQUEST = 400;

/**
 * @brief HTTP status code 404
 */
static int constexpr HTTP_CODE_NOT_FOUND = 404;

/**
 * @brief HTTP status code 401
 */
static int constexpr HTTP_CODE_UNAUTHORIZED = 401;

/**
 * @brief HTTP status code 500
 */
static int constexpr HTTP_CODE_INTERNAL_ERROR = 500;

class Webserver {
   public:
    explicit Webserver(uint16_t port = 80);
    static auto beginFS(bool formatIfFailed = false) -> bool;
    void begin();
    auto handleClient() -> void;
    void on(const String& uri, HTTPMethod method, std::function<void()> handler);
    void on(const String& uri, std::function<void()> handler);
    void serveStaticC(const char* uriC, const char* pathC, const char* contentTypeC = nullptr,
                      int cacheSeconds = 86400);
    void registerStaticDir(const String& fsDir, const String& uriPrefix, const String& contentType);
    void registerGenericStaticFallback(const String& fsBasePath = "/web", bool excludeRoot = true);
    void onNotFound(std::function<void()> handler);
    ESP8266WebServer& raw();

    auto armLifetimeWindow(uint16_t lifetimeSeconds) -> void;
    auto disarmLifetimeWindow() -> void;
    auto setBusy(bool busy) -> void;
    auto isBusy() const -> bool;
    auto isClosed() const -> bool;
    auto secondsRemaining() const -> uint16_t;

   private:
    ESP8266WebServer _server;

    // Lifetime window: when armed, the server stops accepting connections
    // once the window elapses. Recovery is a power-cycle, by design.
    bool _lifetimeArmed = false;
    bool _closed = false;
    uint32_t _lifetimeExpiresAtMs = 0;
    uint16_t _lifetimeSeconds = 0;

    // Set while an upload or OTA is in flight so the window cannot cut it off.
    bool _busy = false;

    auto checkLifetimeWindow() -> void;

    static const char* guessContentTypeC(const char* path);
};

#endif  // WEB_SERVER_H
