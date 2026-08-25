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

#include "screens/PhotoAlbumScreen.h"
#include "screens/DrawUtils.h"
#include "display/DisplayManager.h"
#include "display/Jpeg.h"
#include "config/ConfigManager.h"
#include "storage/FileStore.h"

extern ConfigManager configManager;

namespace {
constexpr const char* TAG = "PhotoAlbum";
constexpr uint16_t BG_COLOR = LCD_BLACK;
constexpr uint16_t MUTED = 0x8410;
constexpr int16_t PANEL_CENTER = 120;
constexpr int16_t EMPTY_TITLE_Y = 104;
constexpr int16_t EMPTY_HINT_Y = 132;
constexpr int16_t EMPTY_HINT2_Y = 146;
constexpr int16_t ERROR_TEXT_Y = 112;
constexpr uint32_t MS_PER_SECOND = 1000;

// Below three pictures a shuffle is indistinguishable from plain alternation,
// so it is not worth the arithmetic.
constexpr uint16_t MIN_SHUFFLE_IMAGES = 3;
constexpr uint8_t MAX_STEP_ATTEMPTS = 8;

/**
 * @brief Greatest common divisor, used to pick a coprime shuffle stride
 */
auto greatestCommonDivisor(uint16_t lhs, uint16_t rhs) -> uint16_t {
    while (rhs != 0) {
        const uint16_t remainder = lhs % rhs;
        lhs = rhs;
        rhs = remainder;
    }

    return lhs;
}
}  // namespace

/**
 * @brief Number of displayable images currently on the device
 *
 * @return The count
 */
auto PhotoAlbumScreen::imageCount() -> uint16_t { return FileStore::count(FileStore::IMAGE_DIR); }

/**
 * @brief Start the slideshow
 *
 * @return void
 */
void PhotoAlbumScreen::enter() {
    DisplayManager::getGfx()->fillScreen(BG_COLOR);

    _gifPlaying = false;
    _index = 0;
    _currentName = "";

    const uint16_t total = PhotoAlbumScreen::imageCount();
    _empty = (total == 0);

    if (_empty) {
        DrawUtils::drawText(PANEL_CENTER, EMPTY_TITLE_Y, "No pictures yet", 2, LCD_WHITE, BG_COLOR, ALIGN_CENTER);
        DrawUtils::drawText(PANEL_CENTER, EMPTY_HINT_Y, "Upload some on the", 1, MUTED, BG_COLOR, ALIGN_CENTER);
        DrawUtils::drawText(PANEL_CENTER, EMPTY_HINT2_Y, "Pictures page", 1, MUTED, BG_COLOR, ALIGN_CENTER);

        return;
    }

    // Seed from the boot-time jitter available on this chip, so a shuffling
    // album does not open on the same picture after every power cycle.
    randomSeed(micros() ^ EspClass::getCycleCount());

    const PictureSettings& cfg = configManager.settings.pictures;

    if (cfg.auto_display && cfg.shuffle && total >= MIN_SHUFFLE_IMAGES) {
        PhotoAlbumScreen::reseedShuffle(total);
        _index = static_cast<uint16_t>(random(0, total));
        // showCurrent() below displays this picture without going through
        // advance(), so account for it here or the pass runs one image long
        // and repeats this one right before the next reseed.
        _shuffleRemaining--;
    }

    // With auto display off the user pinned one file; honour it and hold.
    if (!cfg.auto_display && !cfg.current.empty()) {
        const uint16_t pinned = FileStore::indexOf(FileStore::IMAGE_DIR, cfg.current.c_str());
        if (pinned != FileStore::NOT_FOUND) {
            _index = pinned;
        }
    }

    showCurrent();
}

/**
 * @brief Stop any animation the slideshow started
 *
 * @return void
 */
void PhotoAlbumScreen::leave() {
    if (_gifPlaying) {
        DisplayManager::stopGifQuiet();
        _gifPlaying = false;
    }
}

/**
 * @brief Advance the slideshow when the dwell time has elapsed
 *
 * @return void
 */
void PhotoAlbumScreen::tick() {
    if (_empty) {
        return;
    }

    const PictureSettings& cfg = configManager.settings.pictures;
    if (!cfg.auto_display) {
        return;
    }

    const uint32_t dwellMs = static_cast<uint32_t>(cfg.interval_s) * MS_PER_SECOND;
    if ((millis() - _shownAtMs) < dwellMs) {
        return;
    }

    if (advance()) {
        showCurrent();
    }
}

/**
 * @brief Move to the next image, wrapping at the end
 *
 * @return true when there is something to show
 */
auto PhotoAlbumScreen::advance() -> bool {
    const uint16_t total = PhotoAlbumScreen::imageCount();
    if (total == 0) {
        _empty = true;
        return false;
    }

    if (!configManager.settings.pictures.shuffle || total < MIN_SHUFFLE_IMAGES) {
        _index = static_cast<uint16_t>((_index + 1) % total);

        return true;
    }

    // Reshuffle when a pass completes, or when pictures were added or deleted
    // and the stride may no longer be coprime with the album size.
    if (total != _shuffleTotal || _shuffleRemaining == 0) {
        PhotoAlbumScreen::reseedShuffle(total);
    }

    _index = static_cast<uint16_t>((_index + _shuffleStep) % total);
    _shuffleRemaining--;

    return true;
}

/**
 * @brief Choose a fresh stride for the next shuffle pass
 *
 * A stride coprime with the album size visits every picture exactly once
 * before repeating, so nothing is starved and nothing shows twice in a row.
 * That costs three counters instead of an N-entry permutation, which matters
 * on a device with well under 30KB of heap.
 *
 * @param total Number of pictures currently in the album
 *
 * @return void
 */
void PhotoAlbumScreen::reseedShuffle(uint16_t total) {
    _shuffleTotal = total;
    _shuffleRemaining = total;
    _shuffleStep = 1;

    for (uint8_t attempt = 0; attempt < MAX_STEP_ATTEMPTS; ++attempt) {
        const auto candidate = static_cast<uint16_t>(random(1, total));

        if (greatestCommonDivisor(candidate, total) == 1) {
            _shuffleStep = candidate;
            break;
        }
    }

    // Falling through with a stride of 1 just means this pass runs in order,
    // which is a harmless outcome rather than a failure.
}

/**
 * @brief Draw the image at the current index
 *
 * @return void
 */
void PhotoAlbumScreen::showCurrent() {
    if (_gifPlaying) {
        DisplayManager::stopGifQuiet();
        _gifPlaying = false;
    }

    String name;
    if (!FileStore::nameAt(FileStore::IMAGE_DIR, _index, name)) {
        _empty = true;
        return;
    }

    _currentName = name;
    _shownAtMs = millis();

    const String path = String(FileStore::IMAGE_DIR) + "/" + name;

    if (Jpeg::isJpegName(name)) {
        DisplayManager::getGfx()->fillScreen(BG_COLOR);

        if (!Jpeg::drawFile(path, true)) {
            DrawUtils::drawText(PANEL_CENTER, ERROR_TEXT_Y, "Bad image", 2, MUTED, BG_COLOR, ALIGN_CENTER);
        }

        return;
    }

    // Anything else in the album is a GIF; hand it to the animator full screen.
    _gifPlaying = DisplayManager::playGifFullScreen(path);

    if (!_gifPlaying) {
        DisplayManager::getGfx()->fillScreen(BG_COLOR);
        DrawUtils::drawText(PANEL_CENTER, ERROR_TEXT_Y, "Bad image", 2, MUTED, BG_COLOR, ALIGN_CENTER);

        Logger::warn((String("Failed to play ") + path).c_str(), TAG);
    }
}
