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

#include <ctime>
#include "Logger.h"

/**
 * @brief Logs a message with a specified log level
 *
 * @param level The severity level of the log message
 * @param message The message to be logged
 * @param className optional class name for context
 */
void Logger::log(LogLevel level, const char* message, const char* className) {
    char entry[LOG_ENTRY_MAX_LEN];
    char timeBuf[12];
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    snprintf(timeBuf, sizeof(timeBuf), "[%02d:%02d:%02d]", now->tm_hour, now->tm_min, now->tm_sec);

    const char* classStr = (className != nullptr && className[0] != '\0') ? className : "Global";

    snprintf(entry, sizeof(entry), "%s(%s)::%s: %s", timeBuf, levelToString(level), classStr, message);

    Serial.println(entry);
}

// debug()/info()/warn()/error() are defined inline in Logger.h so that calls below
// LOG_COMPILE_LEVEL vanish at the call site, taking their string literals with them.

/**
 * @brief Converts a LogLevel enum value to its corresponding string representation
 *
 * @param level The log level
 * @return A constant character pointer to the string representation of the log level
 *         Returns "UNKNOWN" if the log level is not recognized
 */
const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG:
            return "DEBUG";
        case LOG_INFO:
            return "INFO";
        case LOG_WARN:
            return "WARN";
        case LOG_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
