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
#include <TJpg_Decoder.h>

#include "display/Jpeg.h"
#include "display/DisplayManager.h"

namespace {

constexpr const char* TAG = "Jpeg";

bool decoderReady = false;

/**
 * @brief TJpg_Decoder block callback, pushing one MCU block to the panel
 *
 * Returning true asks the decoder to continue with the next block.
 */
auto jpegBlock(int16_t xPos, int16_t yPos, uint16_t width, uint16_t height, uint16_t* bitmap) -> bool {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr) {
        return false;
    }

    // Stop cleanly once the decoder walks past the bottom of the panel.
    if (yPos >= gfx->height()) {
        return false;
    }

    gfx->draw16bitRGBBitmap(xPos, yPos, bitmap, static_cast<int16_t>(width), static_cast<int16_t>(height));

    // A full frame takes long enough to matter to the watchdog.
    EspClass::wdtFeed();
    yield();

    return true;
}

}  // namespace

/**
 * @brief Whether a filename looks like a JPEG
 *
 * @param name The filename
 *
 * @return true for .jpg and .jpeg, case-insensitively
 */
auto Jpeg::isJpegName(const String& name) -> bool {
    String lower = name;
    lower.toLowerCase();

    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

/**
 * @brief Decode a JPEG from LittleFS onto the panel
 *
 * Uses TJpg_Decoder rather than JPEGDEC: the latter holds a ~17.5KB working
 * struct, which cannot be allocated reliably from this device's ~26KB heap
 * once WiFi and the web server have taken their share. TJpg_Decoder works from
 * a roughly 3KB workspace instead.
 *
 * @param path Path to the file on LittleFS
 * @param centered Whether to centre the image on the panel
 *
 * @return true when the image was decoded and drawn
 */
auto Jpeg::drawFile(const String& path, bool centered) -> bool {
    if (!LittleFS.exists(path)) {
        Logger::warn((String("JPEG not found: ") + path).c_str(), TAG);
        return false;
    }

    if (!decoderReady) {
        TJpgDec.setJpgScale(1);

        // Leave the bytes in native order. With swap enabled tjpgd packs
        // RGB565 and then reverses each pixel's bytes (tjpgd.c), which is what
        // TFT_eSPI wants; Arduino_GFX's draw16bitRGBBitmap expects native
        // order, so swapping here scrambles the palette.
        TJpgDec.setSwapBytes(false);
        TJpgDec.setCallback(jpegBlock);
        decoderReady = true;
    }

    int16_t xPos = 0;
    int16_t yPos = 0;

    if (centered) {
        auto* gfx = DisplayManager::getGfx();
        uint16_t width = 0;
        uint16_t height = 0;

        if (gfx != nullptr && TJpgDec.getFsJpgSize(&width, &height, path, LittleFS) == JDR_OK) {
            xPos = static_cast<int16_t>((gfx->width() - static_cast<int16_t>(width)) / 2);
            yPos = static_cast<int16_t>((gfx->height() - static_cast<int16_t>(height)) / 2);

            if (xPos < 0) {
                xPos = 0;
            }
            if (yPos < 0) {
                yPos = 0;
            }
        }
    }

    const JRESULT result = TJpgDec.drawFsJpg(xPos, yPos, path, LittleFS);

    if (result != JDR_OK) {
        Logger::error((String("JPEG decode failed (") + String(static_cast<int>(result)) + "): " + path).c_str(), TAG);

        return false;
    }

    return true;
}
