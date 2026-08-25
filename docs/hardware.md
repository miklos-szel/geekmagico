# Hardware reference — SmallTV Ultra

> This document is upstream work. It is the hardware reverse-engineering from
> [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware),
> preserved here because GeekMagicO is built directly on top of it. Credit for
> the teardown, the pinout and the ST7789 initialization sequence goes there.

- **MCU**: ESP8266
- **LCD controller**: ST7789 (RGB565)
- **Case**: 3d printed

<div align="center">
   <img src="../.github/assets/02-disassembly-tv.jpg" alt="SmallTV disassembly" width="1000" />
   <br>
   <em>SmallTV Ultra disassembly</em>
</div>

## Screen hardware configuration

### Display specifications

- **Controller**: ST7789
- **Resolution**: 240x240 pixels
- **Color Format**: RGB565 (16-bit color)
- **Interface**: SPI (Serial Peripheral Interface)
- **SPI Speed**: 40 MHz (80 MHz is possible, but unstable and outside datasheet spec)
- **Rotation**: Normal (rotation 0) for the SmallTV Ultra

### Pin wiring

The display is connected to the ESP8266 using the following GPIO pins:

| Function      | GPIO Pin | Description                                           |
| ------------- | -------- | ----------------------------------------------------- |
| **MOSI**      | GPIO 13  | SPI Master Out Slave In (data from ESP8266 to screen) |
| **SCK**       | GPIO 14  | SPI Clock                                             |
| **CS**        | GND      | Chip Select, tied permanently to GND                  |
| **DC**        | GPIO 0   | Data/Command select (LOW=command, HIGH=data)          |
| **RST**       | GPIO 2   | Reset pin                                             |
| **Backlight** | GPIO 5   | Backlight control (Active LOW)                        |

### ESP8266 dev board wiring example

For a common ESP8266 dev board such as a NodeMCU or ESP-12E development board, wire the TFT module like this:

| TFT pin        | ESP8266 GPIO | Common dev board label | Notes                                      |
| -------------- | ------------ | ---------------------- | ------------------------------------------ |
| **GND**        | GND          | GND                    | Ground                                     |
| **VCC**        | 3V3          | 3V3                    | Power the display from 3.3V                |
| **SCL / SCK**  | GPIO 14      | D5                     | SPI clock                                  |
| **SDA / MOSI** | GPIO 13      | D7                     | SPI data from ESP8266 to display           |
| **DC**         | GPIO 0       | D3                     | Data/command select                        |
| **RES / RST**  | GPIO 2       | D4                     | Display reset                              |
| **BLK**        | GPIO 5       | D1                     | Optional backlight control, see note below |

If your module has no **CS** pin, or if **CS** is already tied low on the PCB, no extra wiring is needed for chip select.

Many 1.3" ST7789 modules keep the backlight enabled when **BLK** is left floating. In that case the screen will work without connecting **BLK** at all. If you want firmware-controlled backlight, connect **BLK** to GPIO 5.

Be aware that **GPIO 0** and **GPIO 2** are ESP8266 boot strap pins. This wiring matches the original hardware used by the firmware, but if your module pulls either line to the wrong level during reset the ESP8266 may fail to boot.

<div align="center">
   <img src="../.github/assets/03-pinout.jpg" alt="Pinout Diagram" width="1000" />
   <img src="../.github/assets/03-nodemcu.png" alt="NodeMCU working" width="500" />
   <br>
   <em>Pin wiring diagram &amp;&amp; NodeMCU working</em>
</div>

### Important configuration details

**Chip select (CS) polarity**: This board ties CS of the display permanently to GND.

**SPI mode**: SPI Mode 3 (CPOL=1, CPHA=1)

**Data/command pin**: LOW for commands, HIGH for data

**Backlight**: Active-low control - set GPIO 5 LOW to turn the backlight on, HIGH to turn it off

## How the screen works

### Initialization sequence

The firmware initializes the display through the `lcdEnsureInit()` function which performs the following steps:

1. **Backlight activation**: GPIO 5 is configured as output and driven based on the `LCD_BACKLIGHT_ACTIVE_LOW` configuration (typically driven LOW to turn on the backlight)

2. **SPI bus initialization**: Hardware SPI is initialized with:
    - Clock speed: Defined by `LCD_SPI_HZ` (typically 40 MHz)
    - Mode: Defined by `LCD_SPI_MODE` (Mode 3 required for this display)

3. **Hardware reset sequence**: The RST pin (GPIO 2) is toggled with timing:
    - Set HIGH → wait 120ms → Set LOW → wait 120ms → Set HIGH → wait 120ms

4. **Display controller initialization**: A vendor-specific initialization sequence is executed via `lcdRunVendorInit()` which includes:
    - Sleep out (0x11) with 120ms delay
    - Porch settings (0xB2) with parameters: HS=0x1F, VS=0x1F, Dummy=0x00, HBP=0x33, VBP=0x33
    - Tearing effect (0x35) set to OFF (0x00)
    - Memory access control/MADCTL (0x36) set to default (0x00)
    - Color mode (0x3A) set to 16-bit RGB565 (0x05)
    - Power control settings:
        - Power B7 (0xB7) = 0x00
        - Power BB (0xBB) = 0x36
        - Power C0 (0xC0) = 0x2C
        - Power C2 (0xC2) = 0x01
        - Power C3 (0xC3) = 0x13
        - Power C4 (0xC4) = 0x20
        - Power C6 (0xC6) = 0x13
        - Power D0 (0xD0) = 0xA4, 0xA1
        - Power D6 (0xD6) = 0xA1
    - Gamma correction (0xE0, 0xE1) with predefined curves (14 bytes each)
    - Gamma control (0xE4) = 0x1D, 0x00, 0x00
    - Display inversion (0x21)
    - Display ON (0x29)
    - Column address setup (0x2A): 0x00 to 0xEF
    - Row address setup (0x2B): 0x00 to 0xEF
    - RAM write command (0x2C)

5. **Post-initialization**:
    - 10ms delay for display stabilization
    - Display rotation is applied (from configuration via `getLCDRotationSafe()`)
    - Screen is filled with black and text color is set to white

### SPI Communication Protocol

The ST7789 communicates via SPI with the following signal handling:

1. **Clock**: SCK (GPIO 14) - drives the SPI clock at the configured frequency (40 MHz)
2. **Data**: MOSI (GPIO 13) - carries command bytes or pixel data from ESP8266 to display
3. **Chip Select**: CS is permanently tied to GND (always active)
4. **Data/Command mode**: DC pin (GPIO 0) indicates the type of data:
    - DC = LOW: Command byte follows
    - DC = HIGH: Pixel/parameter data follows

The display requires **SPI Mode 3** (CPOL=1, CPHA=1) which is explicitly configured in the initialization sequence.

### Drawing to the screen

The firmware uses the **Arduino_GFX library** with a custom ST7789 display driver. Drawing operations are managed through the global `g_lcd` instance:

1. **Text rendering**:
    - Implemented via `lcdDrawTextWrapped()` which provides word-wrapping support
    - Text wrapping algorithm handles spaces, tabs, and newlines
    - Text size is scaled by integer multipliers (6×8 pixels per character at size 1)
    - Supports automatic line wrapping with configurable character and line limits

2. **Graphics primitives**:
    - Direct access to Arduino_GFX API via `DisplayManager::getGfx()`
    - Rectangle filling: `fillRect(x, y, width, height, color)`
    - Full screen fills: `fillScreen(color)`
    - Direct SPI writes are batched between `beginWrite()` and `endWrite()` calls

3. **GIF playback**:
    - Managed via the `Gif` class instance `s_gif`
    - Supports full-screen GIF playback with optional duration limits
    - Can be stopped at any time via `DisplayManager::stopGif()`

4. **Performance optimizations**:
    - **Hardware SPI**: Uses ESP8266's hardware SPI peripheral (40 MHz) for efficient transfers
    - **Batch writes**: Commands and data are batched between `beginWrite()`/`endWrite()` calls
    - **Yield calls**: `yield()` is called during long operations to prevent watchdog timeout
    - **Direct streaming**: GIF frames are streamed directly without intermediate buffering

### Color format

The display uses **RGB565** (16-bit) color encoding:

- **Red channel**: 5 bits (bits 15-11)
- **Green channel**: 6 bits (bits 10-5)
- **Blue channel**: 5 bits (bits 4-0)

This format provides 65,536 distinct colors and is the standard for ST7789 displays.

**Common color constants** (defined in DisplayManager.h):

- Black: `0x0000`
- White: `0xFFFF`
- Red: `0xF800`
- Green: `0x07E0`
- Blue: `0x001F`
- Cyan: `0x07FF`
- Magenta: `0xF81F`
- Yellow: `0xFFE0`

    etc...
