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

#ifndef NET_HTTPJSON_H
#define NET_HTTPJSON_H

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief Shared plain-HTTP JSON fetch
 *
 * Every outbound call here is plain HTTP - BearSSL costs more heap during a
 * handshake than this device has to spare. Response bodies can be far larger
 * than free heap, so parsing always runs against the stream with a filter
 * rather than buffering, and the watchdog is fed around the blocking parts.
 *
 * Shared rather than duplicated per client so the streaming-parse and
 * watchdog contract lives in one place - a second hand-rolled copy is a
 * second place to get it wrong.
 */
namespace HttpJson {

auto fetchFiltered(const String& url, JsonDocument& filter, JsonDocument& doc, String& status) -> bool;

}  // namespace HttpJson

#endif  // NET_HTTPJSON_H
