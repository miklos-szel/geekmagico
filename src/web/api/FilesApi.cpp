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
#include <LittleFS.h>
#include <Logger.h>

#include "web/Api.h"
#include "web/Webserver.h"
#include "config/ConfigManager.h"
#include "storage/FileStore.h"
#include "screens/ScreenManager.h"

extern ConfigManager configManager;

// NOLINTBEGIN(readability-misplaced-array-index)

namespace {

constexpr const char* TAG = "API::Files";

// Refuse an upload that cannot fit, with a little slack for filesystem
// overhead, rather than filling the volume and failing mid-write.
constexpr size_t FREE_SPACE_SLACK = 8192;

File uploadFile;
bool uploadFailed = false;
String uploadPath;
String uploadMessage;

/**
 * @brief Resolve the ?dir= query parameter to a managed directory
 *
 * Defaults to the photo album, which is what the Pictures page uses.
 */
auto resolveDir(Webserver* webserver) -> const char* {
    if (!webserver->raw().hasArg("dir")) {
        return FileStore::IMAGE_DIR;
    }

    const String requested = webserver->raw().arg("dir");

    if (requested == "gif" || requested == FileStore::GIF_DIR) {
        return FileStore::GIF_DIR;
    }
    if (requested == "image" || requested == FileStore::IMAGE_DIR) {
        return FileStore::IMAGE_DIR;
    }

    return nullptr;
}

void filesList(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const char* dir = resolveDir(webserver);
    if (dir == nullptr) {
        sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "Unknown directory");
        return;
    }

    JsonDocument doc;
    doc["dir"] = dir;

    JsonArray files = doc["files"].to<JsonArray>();
    FileStore::listInto(dir, files);

    doc["totalBytes"] = FileStore::totalBytes();
    doc["usedBytes"] = FileStore::usedBytes();
    doc["freeBytes"] = FileStore::freeBytes();

    sendJson(webserver, HTTP_CODE_OK, doc);
}

void filesDelete(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const char* dir = resolveDir(webserver);
    if (dir == nullptr) {
        sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "Unknown directory");
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    const char* name = doc["name"] | "";
    if (strlen(name) == 0) {
        sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "name is required");
        return;
    }

    if (!FileStore::exists(dir, name)) {
        sendStatus(webserver, HTTP_CODE_NOT_FOUND, "error", "File not found");
        return;
    }

    if (!FileStore::remove(dir, name)) {
        sendStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "error", "Failed to remove file");
        return;
    }

    // Drop any setting that pointed at the file we just deleted.
    if (configManager.settings.pictures.current == name) {
        configManager.settings.pictures.current.clear();
        configManager.save();
    }
    if (configManager.settings.weather.gif == name) {
        configManager.settings.weather.gif.clear();
        configManager.save();
    }

    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "File removed");
}

/**
 * @brief Pin a file: the album image to hold, or the weather animation
 */
void filesSet(Webserver* webserver) {
    if (!requireAuth(webserver)) {
        return;
    }

    const char* dir = resolveDir(webserver);
    if (dir == nullptr) {
        sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error", "Unknown directory");
        return;
    }

    JsonDocument doc;
    if (!readJsonBody(webserver, doc)) {
        return;
    }

    const char* name = doc["name"] | "";
    if (strlen(name) == 0 || !FileStore::exists(dir, name)) {
        sendStatus(webserver, HTTP_CODE_NOT_FOUND, "error", "File not found");
        return;
    }

    if (strcmp(dir, FileStore::GIF_DIR) == 0) {
        configManager.settings.weather.gif = name;
    } else {
        configManager.settings.pictures.current = name;
    }

    if (!configManager.save()) {
        sendStatus(webserver, HTTP_CODE_INTERNAL_ERROR, "error", "Failed to save config");
        return;
    }

    ScreenManager::settingsChanged();

    sendStatus(webserver, HTTP_CODE_OK, "ok", "Selection saved");
}

/**
 * @brief Open the destination file for a starting upload
 *
 * Split out of filesUpload so each phase of the multipart state machine stays
 * small enough to read on its own.
 */
void uploadBegin(Webserver* webserver) {
    uploadFailed = false;
    uploadMessage = "";
    // Cleared up front so a rejected request can't have uploadFinish() act on
    // a still-existing file from the previous, unrelated upload.
    uploadPath = "";

    if (!checkAuth(webserver)) {
        uploadFailed = true;
        uploadMessage = "Authentication required";
        return;
    }

    const char* dir = resolveDir(webserver);
    if (dir == nullptr) {
        uploadFailed = true;
        uploadMessage = "Unknown directory";
        return;
    }

    const String name = FileStore::sanitizeName(webserver->raw().upload().filename);
    if (name.isEmpty() || !FileStore::isDisplayable(name)) {
        uploadFailed = true;
        uploadMessage = "Only JPG and GIF files are accepted";
        return;
    }

    // LittleFS silently refuses to create over-long names; say so plainly
    // rather than surfacing a generic "could not open file".
    if (FileStore::isNameTooLong(name)) {
        uploadFailed = true;
        uploadMessage = String("Filename too long (") + String(name.length()) + " chars, max " +
                        String(FileStore::MAX_NAME_LEN) + ")";
        return;
    }

    if (FileStore::freeBytes() < FREE_SPACE_SLACK) {
        uploadFailed = true;
        uploadMessage = "Not enough free space";
        return;
    }

    FileStore::ensureDir(dir);

    uploadPath = String(dir) + "/" + name;
    uploadFile = LittleFS.open(uploadPath, "w");

    if (!uploadFile) {
        uploadFailed = true;
        uploadMessage = "Could not open file for writing";
        return;
    }

    // Hold off the web lifetime window while bytes are in flight.
    webserver->setBusy(true);

    Logger::info((String("Upload start: ") + uploadPath).c_str(), TAG);
}

/**
 * @brief Append one multipart chunk to the destination file
 */
void uploadWrite(HTTPUpload& upload) {
    if (uploadFailed || !uploadFile) {
        return;
    }

    if (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        uploadFailed = true;
        uploadMessage = "Write failed, filesystem may be full";
    }

    EspClass::wdtFeed();
}

/**
 * @brief Close the destination file, discarding a partial write
 */
void uploadFinish(Webserver* webserver, bool aborted) {
    if (uploadFile) {
        uploadFile.close();
    }

    if ((aborted || uploadFailed) && !uploadPath.isEmpty()) {
        LittleFS.remove(uploadPath);
    }

    if (aborted) {
        uploadFailed = true;
        uploadMessage = "Upload aborted";
        Logger::warn("Upload aborted", TAG);
    }

    webserver->setBusy(false);
}

/**
 * @brief Streaming multipart upload handler
 *
 * One file per request: ESP8266WebServer cannot take concurrent multiparts,
 * which is why the browser uploads a folder sequentially.
 */
void filesUpload(Webserver* webserver) {
    HTTPUpload& upload = webserver->raw().upload();

    switch (upload.status) {
        case UPLOAD_FILE_START:
            uploadBegin(webserver);
            break;
        case UPLOAD_FILE_WRITE:
            uploadWrite(upload);
            break;
        case UPLOAD_FILE_END:
            uploadFinish(webserver, false);
            break;
        case UPLOAD_FILE_ABORTED:
            uploadFinish(webserver, true);
            break;
        default:
            break;
    }
}

/**
 * @brief Reply once the upload body has been consumed
 */
void filesUploadDone(Webserver* webserver) {
    if (uploadFailed) {
        sendStatus(webserver, HTTP_CODE_BAD_REQUEST, "error",
                   uploadMessage.isEmpty() ? "Upload failed" : uploadMessage.c_str());
        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    doc["file"] = uploadPath;
    doc["freeBytes"] = FileStore::freeBytes();

    sendJson(webserver, HTTP_CODE_OK, doc);

    ScreenManager::settingsChanged();
}

}  // namespace

/**
 * @brief Register the directory-scoped media endpoints
 *
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void registerFilesApi(Webserver* webserver) {
    // @openapi {get} /files version=v1 group=Files summary="List media files" requiresAuth=true
    // responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on("/api/v1/files", HTTP_GET, [webserver]() { filesList(webserver); });

    // @openapi {post} /files version=v1 group=Files summary="Upload one media file" requiresAuth=true
    // requestBody=multipart/form-data responses=200:application/json,400:application/json,401:application/json
    webserver->raw().on(
        "/api/v1/files", HTTP_POST, [webserver]() { filesUploadDone(webserver); },
        [webserver]() { filesUpload(webserver); });

    // @openapi {delete} /files version=v1 group=Files summary="Delete a media file" requiresAuth=true
    // requestBody=application/json requestBodySchema=name:string
    // responses=200:application/json,400:application/json,401:application/json,404:application/json
    webserver->raw().on("/api/v1/files", HTTP_DELETE, [webserver]() { filesDelete(webserver); });

    // @openapi {post} /files/set version=v1 group=Files summary="Select the active file" requiresAuth=true
    // requestBody=application/json requestBodySchema=name:string
    // responses=200:application/json,400:application/json,401:application/json,404:application/json
    webserver->raw().on("/api/v1/files/set", HTTP_POST, [webserver]() { filesSet(webserver); });

    Logger::info("Files API registered", TAG);
}

// NOLINTEND(readability-misplaced-array-index)
