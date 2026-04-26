# LedController

A C++ application for driving BK-Light / iPixel 32x32 RGB LED matrix panels over Bluetooth Low Energy. Provides both an interactive terminal UI (TUI) and a scriptable CLI interface.

## Requirements

**Build dependencies**

- C++17 compiler (g++ recommended)
- [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE) — BLE connectivity
- zlib (`libz-dev` on Debian/Ubuntu)
- pkg-config with `dbus-1` and `bluez` available

**Runtime**

- Linux with BlueZ and a BLE 4.0+ adapter
- The panel must be advertising with "LED" anywhere in its device name (standard BK-Light / iPixel firmware)
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

Run without arguments to enter the interactive menu. The application scans for BLE devices whose name contains "LED", presents a numbered list with a confirmation prompt, connects, then opens a full-screen terminal UI navigated with arrow keys.

The TUI remembers your last selected item so you can quickly repeat actions. Press Enter or Space to select, Q or Escape to go back to the previous menu or exit.

```bash
./led_controller
```

### CLI — send text

Scans for all LED devices and sends the text to every one found.

```bash
./led_controller -t "HELLO" [options]
```

### CLI — send file

Automatically detects whether the file is an image (by extension) or a text file. GIF files are sent as raw animated GIFs and played natively by the panel; other images are decoded, scaled, and re-encoded as PNG before transmission.

```bash
./led_controller -f photo.png
./led_controller -f animation.gif
./led_controller -f message.txt [options]
```

### CLI — digital clock

Activate the digital clock with an optional style (0–9, default 0). The current system time is synchronised to the panel before showing the clock face.

```bash
./led_controller -c              # style 0, 12h, no date
./led_controller -c 2            # style 2
./led_controller -c 2 --clock-24h
./led_controller -c 3 --clock-date
```

### CLI — save to slot

Store an image, text, or solid colour permanently in a flash slot (1–100). The content persists after power-off and is replayed on boot.

```bash
./led_controller -f photo.png --save 1
./led_controller -t "Hello" -scroll-h --save 2
./led_controller -f animation.gif --save 5
```

> **Note:** the vertical scroll effect (`-scroll-v`) cannot be saved to a slot because it is rendered entirely in software as a sequence of image frames. Attempting to save text with this effect will store it as `Fixed` instead, with a warning printed.

### CLI — delete slots

```bash
./led_controller --delete-save 1      # remove slot 1
./led_controller --delete-save ALL    # clear all slots (factory reset)
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

These flags apply to `-t` and to `-f` when the target file is a plain text file.

| Flag | Description |
|------|-------------|
| `-scroll-h` | Horizontal scroll left (default) |
| `-scroll-r` | Horizontal scroll right |
| `-scroll-v` | Vertical scroll upward, end-credits style (software rendered; not saveable to slot) |
| `-fixed` | Static display, no animation |
| `-blink` | Blinking effect |
| `-breath` | Breathing / fade effect |
| `-laser` | Laser effect |
| `-s <1-255>` | Scroll speed byte sent to the panel (default 80; higher = faster for hardware effects; lower interval for software scroll) |
| `-p <1-6>` | Font scale (default 1 = base 8x10 px glyph; scale 2 = 16x20 px, 3 = 24x30 px, up to 6) |

Examples:

```bash
./led_controller -t "HELLO" -scroll-h -s 60 -p 1
./led_controller -t "CREDITS" -scroll-v -p 2
./led_controller -f message.txt -scroll-r -s 100
./led_controller -t "SALE" -blink -s 5 --save 3
```

## Interactive menu reference

| Entry | Description |
|-------|-------------|
| Solid colour | Fill the panel with one of 7 preset colours: Red, Green, Blue, White, Cyan, Magenta, Yellow |
| Send text | Type a message, then choose effect, speed, and font scale interactively |
| Send image from file | Load PNG, JPG, BMP, GIF, or PPM; scaled to 32x32 with aspect-ratio-preserving letterboxing. GIF files play as native animations |
| Send text from file | Read a plain text file (multiple lines joined with spaces) and display as scrolling text |
| Send pattern | Choose from 18 geometric and gradient test patterns (see pattern list below) |
| Screen OFF | Send a black fill frame to blank the panel |
| Set brightness | Scale pixel values 1–100%; immediately repaints the last solid colour frame |
| Digital clock | Show a digital clock with style 1–10, 12/24h format, and optional alternating date |
| Save to slot | Store an image, solid colour, pattern, or text permanently on device flash (slots 1–100) |
| Delete slots | Remove a single slot or clear all stored content |

## Pattern list

The following 18 patterns are available in the TUI and via `send_pattern` / `save_pattern`:

| Pattern | Description |
|---------|-------------|
| Checker tiny | 1x1 checkerboard |
| Checker small | 2x2 checkerboard |
| Checker medium | 3x3 checkerboard |
| Checker large | 4x4 checkerboard |
| Horizontal lines | Every other row lit |
| Vertical lines | Every other column lit |
| Diagonal lines (/) | Forward diagonal stripes |
| Diagonal lines (\\) | Backward diagonal stripes |
| Zigzag | Staggered diagonal stripes |
| Grid | Lines every 4 pixels in both axes |
| Border square | Single-pixel border around the panel |
| Ring squares | Concentric square rings |
| Crosshair | Horizontal and vertical centre lines |
| Gradient left -> right | Horizontal gradient from bg to fg |
| Gradient right -> left | Horizontal gradient from fg to bg |
| Gradient top -> bottom | Vertical gradient from bg to fg |
| Gradient bottom -> top | Vertical gradient from fg to bg |
| Dots | Single pixels every 4 pixels |

## Save-to-slot details

All display content types can be saved to flash slots 1–100, except the software vertical scroll. Saved content persists after the panel loses power and is replayed on boot.

| Content type | Save command |
|-------------|--------------|
| Image (PNG/JPG/BMP/GIF/PPM) | `--save <slot>` with `-f <image>` |
| Solid colour | TUI only: Save to slot > Save solid colour |
| Pattern | TUI only: Save to slot > Save pattern |
| Text (any hardware effect) | `--save <slot>` with `-t <text>` or TUI |
| Text with `-scroll-v` | Not saveable; stored as Fixed with warning |
| Animated GIF | `--save <slot>` with `-f <animation.gif>` |

Delete operations:

```bash
./led_controller --delete-save 7       # remove slot 7
./led_controller --delete-save ALL     # clear all slots (factory reset)
```

## Device discovery

All CLI commands scan for BLE devices whose identifier contains the string `"LED"` and attempt to connect to every matching device found. For single-panel setups this is transparent. For multi-panel setups reachable from the same BLE adapter, all panels receive the same command simultaneously.

The interactive TUI connects to exactly one device chosen from the scan results.

## Brightness

Brightness is a software multiplier (1–100) applied to pixel RGB values before PNG encoding. No hardware brightness command is used. The valid range is 1–100; default is 100 (full brightness).

In the TUI, changing brightness immediately repaints the last solid colour frame. Text and image frames must be resent manually after a brightness change.

## Digital clock

The digital clock feature synchronises the panel's internal clock to the current system time when activated, then configures the display style. Styles are numbered 0–9 (shown as 1–10 in the TUI). After disconnect, the clock continues running independently on the panel.

Options: 12h / 24h format, optional alternating date display.

## Supported image formats

PNG, JPG/JPEG, BMP, GIF (animated natively), PPM. Images of any aspect ratio are scaled to fit 32x32 with black letterbox or pillarbox bars using bilinear interpolation. No intermediate files are written to disk. GIF files are transmitted as raw data using a dedicated command ID so the panel plays them natively.

## Project structure

| File | Purpose |
|------|---------|
| `main.cpp` | CLI argument parsing, TUI menus, interactive loop, raw-key input (Linux/Windows) |
| `led_sniffer.h` | `LEDController` class declaration; `Color`, `TextEffect`, `TextOptions`, `PatternType` definitions |
| `led_sniffer.cpp` | BLE transport, two-stage handshake, ACK tracking, minimal PNG encoder, BLE frame builder, text payload builder (native type-4 route), pattern generator, software vertical scroll renderer, image and GIF sender, slot management, digital clock, brightness scaling |
| `image_loader.h` | Declaration of `load_image_for_panel` |
| `image_loader.cpp` | Multi-format image loading via stb_image, bilinear resize, aspect-ratio-preserving letterbox compositor; no intermediate files written |
| `stb_image.h` | Single-header image decoder (must be downloaded separately) |
| `makefile` | Build rules |

## BLE protocol notes

The panel uses a GATT service on UUID `000000fa-0000-1000-8000-00805f9b34fb` with two characteristics:

- `FA02` (write) — commands and data
- `FA03` (notify) — acknowledgement responses

**Image frames** go through a two-stage handshake before transmission. Raw RGB is rejected by the panel; all pixel data must be wrapped as a valid PNG, then wrapped in a 15-byte BLE frame header containing the payload length and a CRC32. The frame is sent via `write_request` (acknowledged write). The panel replies with a stage-3 ACK notification before the next frame may be sent. A periodic validation packet is sent every 30 frames to maintain session integrity.

**Text payloads** use a separate four-packet open sequence and are sent via `write_command` (unacknowledged) in 509-byte chunks with 60 ms inter-chunk delays. The native type-4 route is used for all text lengths.

**GIF files** are transmitted as raw GIF data using command ID `0x03` instead of `0x02`. The panel plays the animation natively without any host-side frame decoding.

**Slot-based saving** uses the same frame structure as display-only commands but with a flash slot number (1–100) in place of the temporary display slot `0x65`.

**Digital clock** uses a dedicated command (`0x06, 0x01`) that accepts style, format, and date fields after first writing the current time via the `0x01, 0x80` set-time command.

**ACK variants:** The code handles two known ACK byte sequences for each handshake stage to support both the ACT1026 (32x32) and ACT1025 (64x16) panel variants.

## Known limitations

- Brightness changes do not repaint text or image frames automatically; only the last solid colour frame is repainted.
- The software vertical scroll (`-scroll-v` / `TextEffect::ScrollUp`) cannot be saved to a flash slot; it is converted to `Fixed` with a warning.
- The digital clock runs on the panel's internal clock after disconnect; accuracy depends on the panel's own RTC.
- Multi-panel CLI mode sends to all discovered devices matching "LED" in their name; there is no per-device address filter on the CLI.
