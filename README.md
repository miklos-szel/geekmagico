# GeekMagicO

Open-source firmware for the **GeekMagic SmallTV Ultra** — a full replacement for the stock
closed firmware, with the same feature set and none of the phone-home.

> **This is a fork of [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware).**
>
> All of the hardware reverse-engineering this depends on — the teardown, the pin mapping,
> the ST7789 initialization sequence, the LittleFS/OTA plumbing — is upstream's work, and it
> is preserved in [`docs/hardware.md`](docs/hardware.md). GeekMagicO builds on that
> foundation to reach feature parity with the original GeekMagic firmware.
>
> The original device firmware is by [GeekMagicClock](https://github.com/GeekMagicClock/smalltv-ultra).

[![License: GPLV3](https://img.shields.io/badge/License-GPLV3-yellow.svg?style=for-the-badge)](LICENSE)

---

## What it does

A five-tab web interface — **Network, Weather, Time, Pictures, Settings** — driving a themed
240x240 display:

| | |
|---|---|
| **Weather** | Current conditions and a multi-day forecast. Works with no API key at all; set an OpenWeatherMap key for higher refresh rates. |
| **Clock** | Three time styles plus a simple weather clock. Per-digit colours, 12/24h, date format, colon blink, selectable font. |
| **Photo album** | JPG and GIF slideshow from onboard storage, in order or shuffled, with **folder upload** from the browser. |
| **Themes** | Seven screens, switchable by hand or on a timer. |
| **Display** | Brightness control and a scheduled night mode. |
| **System** | OTA firmware updates, screen rotation, logs, factory reset, rescue boot mode. |

## Supported hardware

**SmallTV Ultra only.** ESP8266 (esp12e), 4MB flash, ST7789 240x240 SPI display.

Earlier versions of the upstream project also supported the HelloCubic Lite; GeekMagicO
targets a single device so the display layouts can be tuned rather than compromised. If you
have a cube, use [upstream](https://github.com/Times-Z/GeekMagic-Open-Firmware).

---

## Migrating from the stock firmware

You do **not** need a USB-to-TTL adapter, and you do not need to open the case. The stock
firmware's own update page is the entry point. Two steps, in this order.

Grab `firmware.bin` and `littlefs.bin` from the [latest release](https://github.com/miklos-szel/geekmagico/releases/latest) first.

### Step 1 — flash the firmware over the stock updater

Point a browser at your device's update page and upload **`firmware.bin`**:

```
http://<your-device-ip>/update
```

The device reboots into GeekMagicO. At this point the screen may look wrong or the
orientation may be off — that is expected, because only half the job is done: the firmware
is flashed but the filesystem (web UI and configuration) is not.

### Step 2 — flash the filesystem

With no filesystem present, GeekMagicO brings up a WiFi access point and exposes a
minimal upload route for exactly this purpose:

- **SSID:** `GeekMagicO`
- **Password:** `$str0ngPa$$w0rd`

Join it, then upload **`littlefs.bin`** at:

```
http://192.168.4.1/legacyupdate
```

The device reboots and the full web UI comes up. Done.

> **Why two steps:** `/legacyupdate` is only registered while LittleFS is empty or
> unmountable — that is, exactly the state Step 1 leaves you in. Once the filesystem is
> flashed, the route disappears and normal OTA updates happen through **Settings → Firmware
> update**.

### First boot

Connect to the `GeekMagicO` access point and open <http://192.168.4.1>, then:

1. **Network** — pick your WiFi (2.4GHz only; the ESP8266 has no 5GHz radio) and set your city
2. **Weather** — optionally add an [OpenWeatherMap API key](https://openweathermap.org/api) for faster refreshes
3. **Settings** — choose a theme and set brightness

The device leaves AP mode once it joins your network and shows its new IP on screen.

### Going back to the stock firmware

Restore a full flash backup taken **before** you flashed GeekMagicO. Take one first — see
[`backup/readme.md`](backup/readme.md). Upstream also publishes a factory dump for the
SmallTV Ultra at `backup/Smalltv-Ultra/`.

---

## Security model

The device is a LAN appliance and behaves like one by default. There is **no API token** —
that concept was removed in GeekMagicO. Two opt-in controls replace it, both under
**Settings → Web security**:

**Optional password.** Turn on HTTP Basic auth and set a password. The browser handles the
prompt; the password is stored in the device's obfuscated EEPROM store, never in
`config.json`. Off by default, matching the stock firmware.

**Web lifetime window.** Set the web server to shut down N seconds after boot (60 is a
sensible value; `0` means always on). After the window closes the device serves nothing at
all — useful when it lives on a shelf as a photo frame. It deliberately does **not** arm
while the device is in setup/AP mode, and it will not fire mid-upload or mid-OTA.

> **To get back in, power-cycle the device** — the window reopens on the next boot. There is
> no remote way to reopen it, which is the point.

Forgot the password, or locked yourself out? See [Rescue mode](#rescue-boot-mode).

### On stored secrets

WiFi credentials and the web password live in EEPROM, obfuscated with a key derived from the
device's MAC address, chip ID, and a build-time salt via SHA-256. This raises the bar against
casual flash dumps. It is **not** encryption: there is no secure element, and anyone who
knows the salt, MAC and chip ID can reconstruct the key. Do not assume confidentiality
against a determined attacker with physical access.

---

## Building

Requires [PlatformIO](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html).

```bash
cp data/config.example data/config.json
pio run                  # firmware.bin
pio run --target buildfs # littlefs.bin
```

Output lands in `.pio/build/esp12e/`. There is also a devcontainer (`build` / `buildfs`
aliases) and `./scripts/build-with-docker.sh`.

`data/config.json` is gitignored. WiFi credentials placed there are migrated into EEPROM on
first boot and erased from the file.

### Stack

| Component | Technology |
|---|---|
| MCU | ESP8266 (esp12e) |
| Build | PlatformIO + Arduino framework |
| Filesystem | LittleFS |
| Display | [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) (ST7789, SPI, RGB565) |
| Decoders | [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) |
| Web UI | [Pico.css](https://picocss.com/docs) + [Alpine.js](https://alpinejs.dev/), vendored — no CDN |

## API

The device exposes a JSON API over HTTP. See [`swagger.yml`](swagger.yml), which is generated
from `// @openapi` comments in the source — run `./scripts/openapi.sh` after changing a
handler rather than editing it by hand.

## Rescue boot mode

If the device fails to boot several times in a row, it enters **Rescue Mode**: a minimal
access point and recovery API that works even when the main firmware is broken.

- `GET /api/v1/rescue/status` — system and debug info
- `POST /api/v1/rescue/reboot` — reboot
- `POST /api/v1/rescue/ota` — upload new firmware (multipart)
- `POST /api/v1/rescue/factory-reset` — clear settings, including a forgotten web password

Rescue Mode uses the same AP credentials as setup mode and requires no authentication —
it is the deliberate escape hatch. The crash counter resets automatically after a stable boot.

## Contributing

Issues and pull requests welcome at [github.com/miklos-szel/geekmagico](https://github.com/miklos-szel/geekmagico). CI runs `pio check --fail-on-defect high` (clang-tidy) and
a full build; both must pass. See [`CLAUDE.md`](CLAUDE.md) for the conventions this codebase
holds to.

## Contact

Questions, bug reports, or anything else: [hello@miklos-szel.com](mailto:hello@miklos-szel.com)

## License

GPLv3 — see [LICENSE](LICENSE). Copyright is shared between the upstream author (Times-Z) and
GeekMagicO contributors; upstream notices are preserved in every file per the license terms.

## Credits

- [Times-Z](https://github.com/Times-Z/GeekMagic-Open-Firmware) — the open firmware this forks, and all the hardware reverse-engineering
- [GeekMagicClock](https://github.com/GeekMagicClock/smalltv-ultra) — the original device
