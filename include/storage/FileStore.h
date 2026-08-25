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

#ifndef STORAGE_FILESTORE_H
#define STORAGE_FILESTORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstdint>

/**
 * @brief Directory-scoped media storage shared by the album and the API
 *
 * Two directories are managed: /image holds the photo album (JPG and GIF) and
 * /gif holds the small animations used on the weather screen.
 */
class FileStore {
   public:
    static constexpr const char* IMAGE_DIR = "/image";
    static constexpr const char* GIF_DIR = "/gif";
    static constexpr uint16_t NOT_FOUND = 0xFFFF;

    // LittleFS on the ESP8266 is built with LFS_NAME_MAX = 32 and rejects any
    // path component where strlen >= that, so 31 characters is the ceiling.
    static constexpr size_t MAX_NAME_LEN = 31;

    static auto isValidDir(const char* dir) -> bool;
    static void ensureDir(const char* dir);

    static auto sanitizeName(const String& raw) -> String;
    static auto isDisplayable(const String& name) -> bool;
    static auto isNameTooLong(const String& name) -> bool;

    static auto count(const char* dir) -> uint16_t;
    static auto nameAt(const char* dir, uint16_t index, String& out) -> bool;
    static auto indexOf(const char* dir, const char* name) -> uint16_t;
    static auto exists(const char* dir, const String& name) -> bool;
    static auto remove(const char* dir, const String& name) -> bool;

    static void listInto(const char* dir, JsonArray files);

    static auto totalBytes() -> size_t;
    static auto usedBytes() -> size_t;
    static auto freeBytes() -> size_t;
};

#endif  // STORAGE_FILESTORE_H
