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
#include <array>
#include <cstring>

#include "display/IconBitmap.h"
#include "display/DisplayManager.h"

namespace {

constexpr const char* TAG = "IconBitmap";

constexpr size_t MAGIC_LEN = 4;
constexpr size_t HEADER_SIZE = 8;
constexpr size_t OFFSET_WIDTH = 4;
constexpr size_t OFFSET_HEIGHT = 5;
constexpr size_t OFFSET_BPP = 6;
constexpr size_t OFFSET_PALETTE_LEN = 7;
constexpr size_t BYTES_PER_COLOR = 2;
constexpr uint8_t NIBBLE_MASK = 0x0F;
constexpr uint8_t NIBBLE_SHIFT = 4;
constexpr uint8_t BYTE_SHIFT = 8;

constexpr std::array<char, MAGIC_LEN> MAGIC = {'W', 'X', 'I', '1'};

constexpr size_t MAX_STRIDE = (IconBitmap::MAX_DIMENSION + 1) / 2;

/**
 * @brief Header of a '.wxi', once validated
 */
struct IconHeader {
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t paletteLen = 0;
    size_t stride = 0;
};

/**
 * @brief Read and fully validate a '.wxi' header
 *
 * Every field is bounds-checked here rather than at the point of use: `width`
 * and `height` come off the filesystem as raw bytes and index the caller's
 * fixed stack buffers, so an unchecked 255 would be a stack overrun from a
 * file. The declared geometry is also checked against the real file length, so
 * truncation is caught before anything reaches the panel.
 *
 * @param file Open file, positioned at the start
 * @param out Header fields, only written when this returns true
 *
 * @return true when the file is a '.wxi' this build can draw
 */
auto readHeader(File& file, IconHeader& out) -> bool {
    std::array<uint8_t, HEADER_SIZE> header{};
    if (file.read(header.data(), header.size()) != static_cast<int>(header.size())) {
        return false;
    }

    if (memcmp(header.data(), MAGIC.data(), MAGIC_LEN) != 0) {
        return false;
    }

    const uint8_t width = header[OFFSET_WIDTH];
    const uint8_t height = header[OFFSET_HEIGHT];
    const uint8_t paletteLen = header[OFFSET_PALETTE_LEN];

    if (width == 0 || width > IconBitmap::MAX_DIMENSION || height == 0 || height > IconBitmap::MAX_DIMENSION ||
        header[OFFSET_BPP] != IconBitmap::BITS_PER_PIXEL || paletteLen == 0 || paletteLen > IconBitmap::MAX_PALETTE) {
        return false;
    }

    const size_t stride = (static_cast<size_t>(width) + 1) / 2;
    const size_t expected = HEADER_SIZE + (static_cast<size_t>(paletteLen) * BYTES_PER_COLOR) + (stride * height);

    if (file.size() < expected) {
        return false;
    }

    out.width = width;
    out.height = height;
    out.paletteLen = paletteLen;
    out.stride = stride;

    return true;
}

/**
 * @brief Read the palette that follows the header
 *
 * Entries are assembled with explicit shifts rather than a cast over the read
 * buffer. The file is little-endian by definition and Arduino_GFX reads this
 * array in native order -- see the setSwapBytes note in Jpeg.cpp -- so doing
 * the arithmetic makes the contract independent of the host's endianness and
 * of the buffer's alignment.
 *
 * @param file Open file, positioned at the palette
 * @param paletteLen Number of entries to read
 * @param out Palette, value-initialised so unused slots are defined
 *
 * @return true when the whole palette was read
 */
auto readPalette(File& file, uint8_t paletteLen, std::array<uint16_t, IconBitmap::MAX_PALETTE>& out) -> bool {
    const size_t paletteBytes = static_cast<size_t>(paletteLen) * BYTES_PER_COLOR;

    std::array<uint8_t, IconBitmap::MAX_PALETTE * BYTES_PER_COLOR> buffer{};
    if (file.read(buffer.data(), paletteBytes) != static_cast<int>(paletteBytes)) {
        return false;
    }

    for (size_t entry = 0; entry < paletteLen; ++entry) {
        const auto low = static_cast<uint16_t>(buffer[entry * BYTES_PER_COLOR]);
        const auto high = static_cast<uint16_t>(buffer[(entry * BYTES_PER_COLOR) + 1]);
        out[entry] = static_cast<uint16_t>(low | static_cast<uint16_t>(high << BYTE_SHIFT));
    }

    return true;
}

}  // namespace

/**
 * @brief Draw a palette-indexed still image from LittleFS
 *
 * One row of pixels is expanded at a time and pushed straight to the panel, so
 * an icon of any supported size costs a fixed ~230 bytes of stack and nothing
 * on the heap. An 80x80 icon is roughly 4ms of SPI, which is why this is safe
 * to call inline from a screen's tick().
 *
 * @param path Absolute LittleFS path to the '.wxi' file
 * @param xPos Left edge on the panel
 * @param yPos Top edge on the panel
 *
 * @return true when the image was drawn in full
 */
auto IconBitmap::draw(const char* path, int16_t xPos, int16_t yPos) -> bool {
    auto* gfx = DisplayManager::getGfx();
    if (gfx == nullptr || path == nullptr) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    IconHeader header;
    std::array<uint16_t, MAX_PALETTE> palette{};

    if (!readHeader(file, header) || !readPalette(file, header.paletteLen, palette)) {
        file.close();
        Logger::warn((String("Unusable icon, drawing the glyph instead: ") + path).c_str(), TAG);
        return false;
    }

    // Non-const on purpose: Arduino_GFX overloads draw16bitRGBBitmap on
    // constness, and only the uint16_t* overload reaches the bulk writePixels
    // path. A const buffer silently selects the per-pixel PROGMEM version.
    std::array<uint8_t, MAX_STRIDE> rowBuf{};
    std::array<uint16_t, MAX_DIMENSION> line{};
    bool complete = true;

    for (uint8_t row = 0; row < header.height; ++row) {
        if (file.read(rowBuf.data(), header.stride) != static_cast<int>(header.stride)) {
            // A short read here is an I/O fault, not a malformed file -- the
            // length was already checked. Paint the remainder in the
            // background colour so the caller's fallback lands on a clean slot
            // rather than over half an icon.
            line.fill(palette[0]);
            for (uint8_t rest = row; rest < header.height; ++rest) {
                gfx->draw16bitRGBBitmap(xPos, static_cast<int16_t>(yPos + rest), line.data(), header.width, 1);
            }
            complete = false;
            break;
        }

        for (uint8_t column = 0; column < header.width; ++column) {
            const uint8_t packed = rowBuf[column / 2];
            const uint8_t index = ((column % 2) == 0) ? static_cast<uint8_t>(packed >> NIBBLE_SHIFT)
                                                      : static_cast<uint8_t>(packed & NIBBLE_MASK);
            line[column] = palette[index];
        }

        gfx->draw16bitRGBBitmap(xPos, static_cast<int16_t>(yPos + row), line.data(), header.width, 1);
    }

    file.close();

    // Once per icon, not per row: the whole draw is milliseconds against a 2s
    // watchdog, and yielding mid-icon would hand the SDK the bus for nothing.
    EspClass::wdtFeed();

    return complete;
}
