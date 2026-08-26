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

#ifndef TIME_TIMEZONECLIENT_H
#define TIME_TIMEZONECLIENT_H

#include <Arduino.h>

/**
 * @brief Keyless UTC offset lookup for automatic timezone mode
 *
 * Automatic mode used to depend entirely on OpenWeatherMap reporting the
 * configured city's offset, which left every keyless install running on UTC.
 * This resolves an offset from the device's public IP instead, over plain
 * HTTP like every other outbound call here (see WeatherClient for why there
 * is no TLS). It only runs when the weather provider has not already supplied
 * a city offset, so a keyed setup costs no extra requests.
 */
class TimeZoneClient {
   public:
    static void begin();
    static void loop();
    static auto refreshNow() -> bool;
    static auto lastStatus() -> const String&;
};

#endif  // TIME_TIMEZONECLIENT_H
