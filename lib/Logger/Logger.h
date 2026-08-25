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

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

/**
 * Compile-time log floor. Calls below it become no-ops that the compiler inlines away,
 * taking their message string literals out of flash with them -- which is the point:
 * ~70 debug/info call sites were costing roughly 4KB of an image that has to clear the
 * stock updater's OTA ceiling (see CLAUDE.md).
 *
 * A plain integer, not the enum, because the preprocessor cannot see enumerators.
 * 0 = debug, 1 = info, 2 = warn, 3 = error. Set via -DLOG_COMPILE_LEVEL in platformio.ini.
 *
 * Note this is a *floor*, not the runtime filter: LOG_MIN_LEVEL still decides what reaches
 * the retrievable ring buffer.
 */
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL 2
#endif

static constexpr LogLevel LOG_MIN_LEVEL = LOG_WARN;
static constexpr size_t LOG_BUFFER_MAX_ENTRIES = 20;
static constexpr size_t LOG_ENTRY_MAX_LEN = 96;

class Logger {
   public:
    static void log(LogLevel level, const char* message, const char* className = nullptr);

    static void debug(const char* message, const char* className = nullptr) {
#if LOG_COMPILE_LEVEL <= 0
        log(LOG_DEBUG, message, className);
#else
        (void)message;
        (void)className;
#endif
    }

    static void info(const char* message, const char* className = nullptr) {
#if LOG_COMPILE_LEVEL <= 1
        log(LOG_INFO, message, className);
#else
        (void)message;
        (void)className;
#endif
    }

    static void warn(const char* message, const char* className = nullptr) {
#if LOG_COMPILE_LEVEL <= 2
        log(LOG_WARN, message, className);
#else
        (void)message;
        (void)className;
#endif
    }

    static void error(const char* message, const char* className = nullptr) {
#if LOG_COMPILE_LEVEL <= 3
        log(LOG_ERROR, message, className);
#else
        (void)message;
        (void)className;
#endif
    }

    static String getLogsAsString();
    static size_t getLogCount();
    static const char* getLogEntry(size_t index);
    static void clearLogs();

   private:
    static void printTime();
    static const char* levelToString(LogLevel level);
    static void addToBuffer(const char* entry);
    static void ensureBufferAllocated();

    static char* _logBuffer;
    static size_t _head;
    static size_t _count;
};

#endif  // LOGGER_H
