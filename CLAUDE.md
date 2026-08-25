# GeekMagicO — working notes

Open firmware for the **GeekMagic SmallTV Ultra** (ESP8266). A fork of
[Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware), aiming
for feature parity with the original closed firmware.

## Target

**SmallTV Ultra only.** HelloCubic Lite support was deliberately dropped so display layouts
can be tuned for one panel rather than compromised across two. Default `LCD_ROTATION` is `0`
(`include/config/ConfigManager.h`); the cube's `4` is gone. Don't reintroduce a second target
without a reason — it doubles every layout decision.

## The budget that actually binds

**RAM, not flash.** Flash is ~1MB with room to spare (`eagle.flash.4m2m.ld`, 2MB LittleFS).
Static RAM sits around 52% of 81,920 bytes, leaving roughly **39KB of heap** for WiFi
buffers, the web server, JSON documents and decoders.

Check every build's `RAM:` line and treat a regression as a bug. At runtime, `src/main.cpp`
logs free heap every 10 seconds — that number is the tripwire. **Target ≥18KB free at idle.**

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
reachable over `http://`. If you need a new external service, check it serves plain HTTP
before designing around it.

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

## Licensing

GPL-3.0. Every source file carries upstream's `Copyright (C) 2026 Times-Z` line alongside the
fork's. **Preserving those notices is a license requirement, not a courtesy** — new files get
the same header block; existing ones keep what they have.

## CI

`.github/workflows/ci.yml` gates on shellcheck, `pio check --fail-on-defect high`
(clang-tidy: `bugprone-*`, `modernize-*`, `readability-*`, warnings as errors) and a full
build. Run `pio check --fail-on-defect high` before pushing — the clang-tidy profile is
strict and catches things like implicit conversions and missing trailing return types.

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
