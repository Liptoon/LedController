# LedController

A C++ application for driving BK-Light / iPixel 32x32 RGB LED matrix panels over Bluetooth Low Energy. Provides both an interactive terminal UI and a scriptable CLI interface.

## Requirements

**Build dependencies**

- C++17 compiler (g++ recommended)
- [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE) — BLE connectivity
- zlib (`libz-dev` on Debian/Ubuntu)
- pkg-config with `dbus-1` and `bluez` available

**Runtime**

- Linux with BlueZ and a BLE 4.0+ adapter
- The panel must be advertising with "LED" in its device name (standard BK-Light / iPixel firmware)
- [stb_image.h](https://github.com/nothings/stb/blob/master/stb_image.h) — place this single header in the project root before building

## Building

```bash
make
```

The resulting binary is `led_controller`. To clean:

```bash
make clean
```

The makefile compiles `main.cpp`, `led_sniffer.cpp`, and `image_loader.cpp` with `-std=c++17` and links against SimpleBLE, dbus-1, bluez, and zlib.

## Usage

### Interactive TUI

Run without arguments to enter the interactive menu. The application scans for BLE devices whose name contains "LED", presents a confirmation prompt, connects, then opens a full-screen terminal UI navigated with arrow keys. Press Enter or Space to select, Q or Escape to go back or exit.

```bash
./led_controller
```

### CLI — send text

```bash
./led_controller -t "HELLO" [options]
```

### CLI — send file

Automatically detects whether the file is an image (by extension) or a text file.

```bash
./led_controller -f photo.png
./led_controller -f message.txt [options]
```

### CLI — screen off

Sends a black fill frame to all discovered panels and exits.

```bash
./led_controller -b
```

### Help

```bash
./led_controller -h
```

## Text options

These flags apply to `-t` and `-f` (text files only).

| Flag | Description |
|---|---|
| `-scroll-h` | Horizontal scroll left (default) |
| `-scroll-r` | Horizontal scroll right |
| `-scroll-v` | Vertical scroll upward, end-credits style (software rendered) |
| `-fixed` | Static display, no animation |
| `-blink` | Blinking effect |
| `-breath` | Breathing / fade effect |
| `-laser` | Laser effect |
| `-s <1-255>` | Scroll speed (default 80; higher = faster) |
| `-p <1-6>` | Font scale (default 1 = base 8x10 px glyph; scale 2 = 16x20, 3 = 24x30, ...) |

Examples:

```bash
./led_controller -t "HELLO" -scroll-h -s 60 -p 1
./led_controller -t "CREDITS" -scroll-v -p 2
./led_controller -f message.txt -scroll-r -s 100
```

## Interactive menu reference

| Entry | Description |
|---|---|
| Solid colour | Fill panel with one of 7 preset colours (R/G/B/W/Cyan/Magenta/Yellow) |
| Send text | Type a message, then choose effect, speed, and font scale |
| Send image from file | Load PNG, JPG, BMP, GIF (first frame), or PPM; scaled to 32x32 with letterboxing |
| Send text from file | Read a plain text file and display it as scrolling text |
| Send pattern | Choose from 19 geometric and gradient test patterns |
| Screen OFF | Send a black fill frame to blank the panel |
| Set brightness | Scale pixel values 1–100%; immediately repaints the last solid colour frame |

## Supported image formats

PNG, JPG/JPEG, BMP, GIF (first frame only), PPM. Images of any aspect ratio are scaled to fit 32x32 with black letterbox or pillarbox bars, using bilinear interpolation. No intermediate files are written to disk.

## Glyph set

The built-in 8x10 pixel bitmap font covers A–Z (case-insensitive), 0–9, and the following punctuation: `! ? . , - : /`

Any character outside this set is rendered as a space.

## Brightness

Brightness is a software multiplier applied to pixel RGB values before encoding. It does not use any hardware brightness command. The valid range in the TUI is 1–100. The default is 100 (full brightness). Changing brightness in the TUI immediately repaints the last solid colour frame; text and image frames must be resent manually.

## Project structure

| File | Purpose |
|---|---|
| `main.cpp` | CLI argument parsing, TUI menu, interactive loop |
| `led_sniffer.h` | `LEDController` class declaration, `Color`, `TextEffect`, `TextOptions`, `PatternType` |
| `led_sniffer.cpp` | BLE transport, handshake, PNG encoder, frame builder, text payload builder, pattern generator, image sender |
| `image_loader.h` | Declaration of `load_image_for_panel` |
| `image_loader.cpp` | Multi-format image loading via stb_image, bilinear resize, letterbox compositor |
| `stb_image.h` | Single-header image decoder (must be downloaded separately) |
| `makefile` | Build rules |

## BLE protocol notes

The panel uses a GATT service on UUID `000000fa-0000-1000-8000-00805f9b34fb` with two characteristics:

- `FA02` (write) — commands and data
- `FA03` (notify) — acknowledgement responses

Every image frame goes through a two-stage handshake before transmission. Raw RGB data is rejected by the panel; all pixel data must be wrapped as a valid PNG, then wrapped again in a 15-byte BLE frame header containing the payload length and a CRC32. The frame is sent via `write_request` (acknowledged write). The panel replies with a stage-3 ACK notification before the next frame may be sent.

Text payloads use a separate open sequence and are sent via `write_command` (unacknowledged) in 509-byte chunks with 60 ms inter-chunk delays.

See `proto_doc.md` for the full byte-level protocol specification.
