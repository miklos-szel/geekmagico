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
#include <Logger.h>
#include <cstring>

#include "storage/FileStore.h"

namespace {
constexpr const char* TAG = "FileStore";
}

/**
 * @brief Whether a directory is one this store manages
 *
 * Callers pass this straight from a query parameter, so anything unknown must
 * be refused rather than letting a request reach an arbitrary path.
 *
 * @param dir The directory path
 *
 * @return true for a managed directory
 */
auto FileStore::isValidDir(const char* dir) -> bool {
    if (dir == nullptr) {
        return false;
    }

    return strcmp(dir, FileStore::IMAGE_DIR) == 0;
}

/**
 * @brief Create a managed directory if it does not exist
 *
 * @param dir The directory path
 *
 * @return void
 */
void FileStore::ensureDir(const char* dir) {
    if (!FileStore::isValidDir(dir) || LittleFS.exists(dir)) {
        return;
    }

    if (!LittleFS.mkdir(dir)) {
        Logger::error((String("Failed to create ") + dir).c_str(), TAG);
    }
}

/**
 * @brief Reduce an uploaded filename to a bare, safe name
 *
 * Strips any directory component so an upload cannot escape its directory.
 *
 * @param raw The name as supplied by the client
 *
 * @return The bare filename
 */
auto FileStore::sanitizeName(const String& raw) -> String {
    String name = raw;
    name.replace("\\", "/");
    name = name.substring(name.lastIndexOf('/') + 1);
    name.replace("..", "");
    name.trim();

    return name;
}

/**
 * @brief Whether a filename is something the device can display
 *
 * @param name The filename
 *
 * @return true for JPG and GIF
 */
auto FileStore::isDisplayable(const String& name) -> bool {
    String lower = name;
    lower.toLowerCase();

    return lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".gif");
}

/**
 * @brief Whether a filename exceeds what LittleFS can store
 *
 * @param name The filename
 *
 * @return true when the name is too long to create
 */
auto FileStore::isNameTooLong(const String& name) -> bool { return name.length() > FileStore::MAX_NAME_LEN; }

/**
 * @brief Number of displayable files in a directory
 *
 * @param dir The directory path
 *
 * @return The count
 */
auto FileStore::count(const char* dir) -> uint16_t {
    if (!FileStore::isValidDir(dir)) {
        return 0;
    }

    uint16_t total = 0;
    Dir handle = LittleFS.openDir(dir);

    while (handle.next()) {
        if (FileStore::isDisplayable(handle.fileName())) {
            total++;
        }
    }

    return total;
}

/**
 * @brief Name of the nth displayable file in a directory
 *
 * Iteration order is whatever LittleFS reports, which is stable between calls
 * as long as the directory is not modified. That is enough for a slideshow.
 *
 * @param dir The directory path
 * @param index Zero-based position
 * @param out Receives the filename
 *
 * @return true when the index exists
 */
auto FileStore::nameAt(const char* dir, uint16_t index, String& out) -> bool {
    if (!FileStore::isValidDir(dir)) {
        return false;
    }

    uint16_t position = 0;
    Dir handle = LittleFS.openDir(dir);

    while (handle.next()) {
        const String name = handle.fileName();
        if (!FileStore::isDisplayable(name)) {
            continue;
        }

        if (position == index) {
            out = name;
            return true;
        }

        position++;
    }

    return false;
}

/**
 * @brief Position of a named file in a directory
 *
 * @param dir The directory path
 * @param name The filename
 *
 * @return The index, or NOT_FOUND
 */
auto FileStore::indexOf(const char* dir, const char* name) -> uint16_t {
    if (!FileStore::isValidDir(dir) || name == nullptr) {
        return FileStore::NOT_FOUND;
    }

    uint16_t position = 0;
    Dir handle = LittleFS.openDir(dir);

    while (handle.next()) {
        const String entry = handle.fileName();
        if (!FileStore::isDisplayable(entry)) {
            continue;
        }

        if (entry == name) {
            return position;
        }

        position++;
    }

    return FileStore::NOT_FOUND;
}

/**
 * @brief Whether a named file exists in a directory
 *
 * @param dir The directory path
 * @param name The filename
 *
 * @return true when present
 */
auto FileStore::exists(const char* dir, const String& name) -> bool {
    if (!FileStore::isValidDir(dir)) {
        return false;
    }

    return LittleFS.exists(String(dir) + "/" + FileStore::sanitizeName(name));
}

/**
 * @brief Delete a file from a directory
 *
 * @param dir The directory path
 * @param name The filename
 *
 * @return true when the file was removed
 */
auto FileStore::remove(const char* dir, const String& name) -> bool {
    if (!FileStore::isValidDir(dir)) {
        return false;
    }

    const String path = String(dir) + "/" + FileStore::sanitizeName(name);
    if (!LittleFS.exists(path)) {
        return false;
    }

    const bool removed = LittleFS.remove(path);
    if (removed) {
        Logger::info((String("Removed ") + path).c_str(), TAG);
    } else {
        Logger::error((String("Failed to remove ") + path).c_str(), TAG);
    }

    return removed;
}

/**
 * @brief Append a directory listing to a JSON array
 *
 * @param dir The directory path
 * @param files The array to fill
 *
 * @return void
 */
void FileStore::listInto(const char* dir, JsonArray files) {
    if (!FileStore::isValidDir(dir)) {
        return;
    }

    Dir handle = LittleFS.openDir(dir);

    while (handle.next()) {
        const String name = handle.fileName();
        if (!FileStore::isDisplayable(name)) {
            continue;
        }

        JsonObject entry = files.add<JsonObject>();
        entry["name"] = name;              // NOLINT(readability-misplaced-array-index)
        entry["size"] = handle.fileSize();  // NOLINT(readability-misplaced-array-index)
    }
}

/**
 * @brief Total filesystem size
 *
 * @return Size in bytes
 */
auto FileStore::totalBytes() -> size_t {
    FSInfo info{};

    return LittleFS.info(info) ? info.totalBytes : 0;
}

/**
 * @brief Used filesystem space
 *
 * @return Size in bytes
 */
auto FileStore::usedBytes() -> size_t {
    FSInfo info{};

    return LittleFS.info(info) ? info.usedBytes : 0;
}

/**
 * @brief Remaining filesystem space
 *
 * @return Size in bytes
 */
auto FileStore::freeBytes() -> size_t {
    const size_t total = FileStore::totalBytes();
    const size_t used = FileStore::usedBytes();

    return (total > used) ? (total - used) : 0;
}
