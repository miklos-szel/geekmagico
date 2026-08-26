# GeekMagicO — working notes

Open firmware for the **GeekMagic SmallTV Ultra** (ESP8266). A fork of
[Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware), aiming
for feature parity with the original closed firmware.

## Target

**SmallTV Ultra only.** HelloCubic Lite support was deliberately dropped so display layouts
can be tuned for one panel rather than compromised across two. Default `LCD_ROTATION` is `0`
(`include/config/ConfigManager.h`); the cube's `4` is gone. Don't reintroduce a second target
without a reason — it doubles every layout decision.

## The OTA size ceiling

`firmware.bin` **must stay under ~528KB**, and the working target is **≤512KB (524,288
bytes)**. This is not a preference; past it, users cannot install the firmware at all.

Users arrive from the stock firmware by uploading `firmware.bin` through the stock `/update`
page. The ESP8266 core's `Update.begin()` (`Updater.cpp`, `UPDATE_ERROR_SPACE`) refuses the
upload *before writing a byte* when:

```
round4k(runningSketchSize) + round4k(newFirmwareSize) > FS_start_offset
```

On the stock image the running sketch is 505,200 bytes (rounds to 507,904) and the filesystem
starts at the 1MB mark, which leaves **540,672 bytes** for the incoming binary. Exceeding it
produces `ERROR[4]: Not Enough Space` on the stock update page and there is no way forward
except soldering to the serial pads.

The ceiling binds **only on that migration upload**. Once GeekMagicO is running its own 4m2m
layout (FS at `0x400000`), `Update.begin()` has ~1.5MB of headroom, so `/api/v1/ota/fw`,
`/legacyupdate` and the rescue OTA are unconstrained. That asymmetry is why the limit is easy
to blow through without noticing: every OTA *you* do during development will succeed.

Check the `Flash:` line on every build. Several size measures in `platformio.ini` exist purely
to hold this line and should not be removed casually:

- `-DNO_GLOBAL_SPIFFS` — the core instantiates a global `FS SPIFFS` object whose constructor
  has side effects, so `--gc-sections` cannot drop it. This project is LittleFS-only; the flag
  reclaims ~29KB. It only compiles because `lib/TJpg_Decoder` aliases `SPIFFS` to `LittleFS`.
- `lib_ignore = SD, SDFS, ESP8266SdFat` — ~18KB of SdFat for a board with no SD slot, pulled
  in by TJpg_Decoder's `#include <SD.h>`. See `lib/TJpg_Decoder/readme.md` for why that library
  is vendored rather than pulled from `lib_deps`.
- `-DARDUINOJSON_USE_DOUBLE=0` / `-DARDUINOJSON_USE_LONG_LONG=0` — ~6KB. Note `time_t` is
  64-bit on this core, so epoch values going into JSON need an explicit `uint32_t` cast.
- `-DLOG_COMPILE_LEVEL=2` — compiles `Logger::debug`/`info` calls away at the call site so
  their string literals leave flash too (~4KB). Raising verbosity costs flash, not just noise.
- `scripts/strip_scanf_float.py` — removes the framework's forced `-u _scanf_float`. Keep the
  tree free of `scanf`/`sscanf` calls or this silently stops helping.

## The budget that actually binds

**RAM is the other hard limit.** Static RAM sits around 61% of 81,920 bytes, leaving roughly
**31KB of heap** for WiFi buffers, the web server, JSON documents and decoders.

Check every build's `RAM:` line and treat a regression as a bug. At runtime, `src/main.cpp`
logs free heap every 10 seconds — that number is the tripwire. **Target ≥18KB free at idle.**

Note that plain string literals land in `.rodata`, which on this part is DRAM, not
memory-mapped flash — so adding log and error messages costs RAM, not just flash.

Consequences:

- Stream, never buffer. Files go out via `streamFile`; HTTP responses are parsed straight
  from `http.getStream()`, with `DeserializationOption::Filter` when the payload is large.
- Decoders (`AnimatedGIF`, `TJpg_Decoder`) render line- or MCU-block-at-a-time
  into the panel. There is no framebuffer and there is no room for one.
- **JPEGDEC is not usable here** and was removed: it holds a ~17.5KB working
  struct plus ~9KB of DRAM colour tables, and that single contiguous allocation
  fails on this device's heap. TJpg_Decoder does the same job from ~3.1KB.
- TJpg_Decoder must run with `setSwapBytes(false)`. With swap on, tjpgd packs
  RGB565 then reverses each pixel's bytes for TFT_eSPI; Arduino_GFX's
  `draw16bitRGBBitmap` reads native order, so swapping rotates the colour
  channels (white survives, everything else is wrong).
- Prefer `constexpr`/PROGMEM over runtime `String` building in hot paths.

## No TLS on outbound calls

Every outbound request uses **plain HTTP**. BearSSL costs ~22KB of heap during a handshake,
which this device does not have to spare while also running a web server.

This is why weather uses OpenWeatherMap over `http://` and a keyless fallback that is also
reachable over `http://`, and why the timezone lookup uses ip-api.com, whose free tier is
HTTP-only. If you need a new external service, check it serves plain HTTP before designing
around it.

All of them go through `HttpJson::fetchFiltered` (`src/net/HttpJson.cpp`) rather than each
client setting up its own `HTTPClient`, so the streaming-parse and `wdtFeed()` contract is
written once. Sharing it is worth a few hundred bytes of flash, not kilobytes — the linker
already folds most of the duplication — but a second hand-rolled copy is a second place to
get the watchdog wrong.

## Timezone

The clock is UTC end to end: `configTime(0, 0, ...)` never bakes in an offset, and
`TimeService` shifts at render time. In `auto` mode the offset comes from the
highest-precedence source that has one — OpenWeatherMap's city offset, then an ip-api
lookup, then the value cached in `time.auto_offset_min`, then the manual `utc_offset_min`.
Auto used to depend on OWM alone, which left every keyless install silently on UTC; keep
a fallback chain in place if you touch `resolveOffsetSeconds()`.

## Watchdog

`loop()` runs under a 2s hardware watchdog armed at the end of `setup()`. Any operation that
can block longer — an HTTP fetch, a large file walk, a JPEG decode — must call
`EspClass::wdtFeed()` (or `yield()`) inside its inner loop. A watchdog reset three times in a
row drops the device into Rescue Mode, so this failure mode is loud but user-hostile.

## Display

Screens repaint **only what changed**. `src/dashboard/DashboardManager.cpp` is the reference
implementation: it caches last-rendered values and repaints a single rectangle when one
differs. Full-screen redraws on a 40MHz SPI bus visibly flicker and starve the loop.

The ST7789 needs **SPI mode 3** and a vendor init sequence; CS is tied to GND on this board.
Don't "simplify" `lcdRunVendorInit()` — see `docs/hardware.md` for why each step is there.

## The GIF decoder is the biggest single allocation here

`AnimatedGIF` is **24,172 bytes in one block** and `playGifAt()` holds it for as long as the
screen is up. This device idles near 20KB free, so that allocation routinely cannot be met.

Two things make this worse than an ordinary failed malloc, and both cost a released version to
learn:

- On this core, plain `operator new` calls `__unhandled_exception(PSTR("OOM"))` instead of
  returning `nullptr` (`cores/esp8266/abi.cpp`). A `if (p == nullptr)` check after a plain `new`
  is **dead code** — the panic happens first. It reboots as `"Software/System restart"`, not an
  exception or a watchdog, which makes it easy to misread. Use `new (std::nothrow)` for anything
  large, as `Gif::begin()` now does.
- That reboot lands inside `BOOT_STABLE_MS`, so the boot never marks clean. Three of them and the
  device is in Rescue Mode — a crash that looks like a brick to the user.

`Gif::begin()` refuses up front unless the heap can carry the decoder plus 8KB of slack. Winning
the allocation only to starve WiFi and the web server is no better than losing it.

**Per-condition animated weather icons were tried and withdrawn** (v1.3.0/v1.3.1, removed in
v1.3.2). Ten bundled GIFs cost 123KB of filesystem and, more importantly, made the weather screen
allocate the decoder on its own. Replacing it with a purpose-built raw frame format was measured
and does not pay off either: shared-palette per-line RLE came to 355KB and RLE-plus-delta to 314KB
for the same ten icons. Don't retry this without a plan for the 24KB.

## Screen geometry

`weatherGlyph()` branches all fill `center +- 3*unit`. Holding that box is what stops the icon
appearing to jump when conditions change: the slot is repainted, not re-laid-out, so a branch
that draws a different size reads to users as a misaligned icon.

On the weather clock, the condition label and the humidity/wind line are **centred on the full
panel width**, not right-aligned beside the icon. They do not fit beside it — `"Partly cloudy"`
is 156px and `"H100%  100.0km/h"` is 192px against an icon box ending at x=88 — and
right-aligning either one puts its left edge inside the icon. Any new field on that screen
needs the same check: `len * 6 * size` against the space actually left.

## API and docs

`swagger.yml` is **generated**, never hand-edited. Handlers carry `// @openapi` comments that
`scripts/generate_openapi.py` reads; run `./scripts/openapi.sh` after changing a route.

## Auth

There is **no API token** — that concept was removed. Auth is optional HTTP Basic, off by
default, with the password in `SecureStorage` and never in `config.json`. `requireAuth()` in
`src/web/Api.cpp` is the single gate; it returns true immediately when auth is disabled.

Rescue Mode endpoints are intentionally **unauthenticated** — they are the escape hatch when
the device is otherwise unreachable.

## Secrets

`KV_SALT_STR` in `src/main.cpp` feeds SecureStorage key derivation
(`src/config/SecureStorage.cpp`). **Changing it makes every deployed device unable to decrypt
its stored WiFi credentials after an OTA.** It keeps its pre-fork value on purpose. Same goes
for the EEPROM key names.

## Filenames

LittleFS here is built with `LFS_NAME_MAX = 32` and refuses to create any path
component of 32 characters or more, so **31 is the ceiling** for an uploaded
filename. The device reports this explicitly and the web UI shortens longer
names before sending them; `scripts/resize-images.sh` does the same on the
desktop side. Do not let a code path surface this as a generic open failure.

## The migration path must keep working

Users arrive from the stock firmware via a two-step OTA documented in the README:
`firmware.bin` through the stock `/update` page, then `littlefs.bin` through
`/legacyupdate`. That second route is only registered when LittleFS is empty or unmountable
(`src/main.cpp`) — which is exactly the state step 1 leaves behind. **Don't make that route
conditional on anything else**, or first-time installs strand users with no web UI.

## Releases

Release notes **must say which images the user has to flash**. The two go out separately and
the web UI talks to the firmware's API, so a release that changed `data/web` and ships as
"firmware only" leaves the old page reading fields that no longer exist — the UI looks broken
while the device underneath is fine.

Decide it mechanically: `git diff <prev-tag>..<tag> -- data/`. Anything under `data/web`
means both images. Put it at the top of the "Installing" section as a bold one-liner, and
when `littlefs.bin` is required, say that flashing it replaces the whole filesystem —
uploaded pictures and `config.json` are lost, WiFi credentials in EEPROM survive.

Tag as `vX.Y.Z-geekmagico` and build the release binaries **after** tagging, or
`PROJECT_VER_STR` carries a `-N-g<sha>-dev` suffix into what users install.

## Licensing

GPL-3.0. Every source file carries upstream's `Copyright (C) 2026 Times-Z` line alongside the
fork's. **Preserving those notices is a license requirement, not a courtesy** — new files get
the same header block; existing ones keep what they have.

## CI

There is no CI in this repo at the moment — the `.github/` directory was removed in `73c5e95`,
so nothing gates a push automatically. Run the checks yourself before pushing:
`pio run` (watch the `Flash:` and `RAM:` lines) and `pio check --fail-on-defect high`
(clang-tidy: `bugprone-*`, `modernize-*`, `readability-*`, warnings as errors). The clang-tidy
profile is strict and catches things like implicit conversions and missing trailing return
types.

Style follows the existing code: trailing return types (`auto f() -> T`), Doxygen blocks on
functions, `static constexpr` over magic numbers, `NOLINT` with a reason when unavoidable.

## Testing

`test/webServerTest.py` is a mock device that serves `data/web` and fakes the API, so the web
UI can be developed without hardware:

```bash
python3 test/webServerTest.py   # http://localhost:8080
```

Keep its routes in sync when adding endpoints. Browser testing goes through `http://localhost`
— `file://` URLs will not work.
