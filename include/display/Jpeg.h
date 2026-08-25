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

#ifndef DISPLAY_JPEG_H
#define DISPLAY_JPEG_H

#include <Arduino.h>

/**
 * @brief Baseline JPEG rendering straight to the panel
 *
 * Decoding is MCU-block at a time into the display; there is no framebuffer,
 * which is what keeps this affordable on the ESP8266's heap.
 */
class Jpeg {
   public:
    static auto drawFile(const String& path, bool centered = true) -> bool;
    static auto isJpegName(const String& name) -> bool;
};

#endif  // DISPLAY_JPEG_H
