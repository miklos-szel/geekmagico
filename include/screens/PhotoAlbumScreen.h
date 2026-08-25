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

#ifndef SCREENS_PHOTOALBUMSCREEN_H
#define SCREENS_PHOTOALBUMSCREEN_H

#include <Arduino.h>

#include "screens/Screen.h"

/**
 * @brief Slideshow of the images in /image
 *
 * JPEGs are decoded straight to the panel; GIFs are handed to the animator.
 * With auto display off, the file pinned on the Pictures page is shown and
 * held instead of cycling.
 */
class PhotoAlbumScreen : public Screen {
   public:
    void enter() override;
    void tick() override;
    void leave() override;

    auto name() const -> const char* override { return "Photo Album"; }

    static auto imageCount() -> uint16_t;

   private:
    void showCurrent();
    auto advance() -> bool;
    void reseedShuffle(uint16_t total);

    uint16_t _index = 0;

    // Shuffle state. Stepping by a stride coprime with the album size walks
    // every picture exactly once per pass, which needs three counters rather
    // than a permutation array - and RAM is the scarce resource here.
    uint16_t _shuffleStep = 1;
    uint16_t _shuffleTotal = 0;
    uint16_t _shuffleRemaining = 0;

    uint32_t _shownAtMs = 0;
    bool _gifPlaying = false;
    bool _empty = false;
    String _currentName;
};

#endif  // SCREENS_PHOTOALBUMSCREEN_H
