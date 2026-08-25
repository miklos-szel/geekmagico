# TJpg_Decoder (vendored)

Vendored copy of [Bodmer/TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) **v1.1.0**
(upstream licence in `license.txt` — FreeBSD/2-clause BSD, kept verbatim).

It is vendored rather than pulled through `lib_deps` because two upstream lines cost this
firmware ~47KB of flash, and the migration OTA path has no room for it (see the OTA ceiling
section in `CLAUDE.md`).

## Local changes

Two edits, both marked with a `GeekMagicO:` comment in the source:

1. **`User_Config.h` — dropped `#define TJPGD_LOAD_SD_LIBRARY`.**
   Upstream defines it unconditionally, so `TJpg_Decoder.h` does `#include <SD.h>`. That pulls
   `SD` → `SDFS` → `ESP8266SdFat` into the link (~18KB of `.irom0.text`) through SD.cpp's global
   `SDClass SD;`, which `--gc-sections` cannot drop because a global constructor references it.
   The SmallTV Ultra has no SD slot. `platformio.ini` also carries
   `lib_ignore = SD, SDFS, ESP8266SdFat` so the stack cannot come back in through another path.

2. **`TJpg_Decoder.h` — added `#define SPIFFS LittleFS` on the ESP8266 branch.**
   The `drawFsJpg`/`getFsJpgSize` overloads default to `fs::FS &fs = SPIFFS`, which pins the
   core's global `FS SPIFFS` object and ~29KB of SPIFFS code. Aliasing the name to LittleFS lets
   the build define `-DNO_GLOBAL_SPIFFS`. Upstream already does exactly this for RP2040 a few
   lines below. Our own call sites (`src/display/Jpeg.cpp`) pass `LittleFS` explicitly anyway.

## Updating

When taking a newer upstream release, re-apply both edits and re-check the size line — the
`spiffs_*` and `FatFile`/`SdSpiCard` symbols must not reappear in `firmware.elf`.
