#include "led_sniffer.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// Cross-platform raw keypress
// ---------------------------------------------------------------------------
#ifdef _WIN32
#  include <conio.h>
static int read_key()
{
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        int c2 = _getch();
        switch (c2) {
            case 72: return 1000; // Up
            case 80: return 1001; // Down
            case 75: return 1002; // Left
            case 77: return 1003; // Right
        }
        return -1;
    }
    return c;
}
#else
#  include <termios.h>
#  include <unistd.h>

static struct termios saved_termios;
static bool termios_saved = false;

static void enable_raw_mode()
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    if (!termios_saved) { saved_termios = raw; termios_saved = true; }
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void disable_raw_mode()
{
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

static int read_key()
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return -1;
    if (c == 27) { // ESC sequence
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return 1000; // Up
                case 'B': return 1001; // Down
                case 'C': return 1003; // Right
                case 'D': return 1002; // Left
            }
        }
        return 27;
    }
    return (int)c;
}
#endif

// ---------------------------------------------------------------------------
// ANSI helpers
// ---------------------------------------------------------------------------
#define ANSI_RESET        "\033[0m"
#define ANSI_BOLD         "\033[1m"
#define ANSI_REVERSE      "\033[7m"
#define ANSI_HIDE_CUR     "\033[?25l"
#define ANSI_SHOW_CUR     "\033[?25h"
#define ANSI_ALT_ON       "\033[?1049h"
#define ANSI_ALT_OFF      "\033[?1049l"
#define ANSI_HOME         "\033[H"
#define ANSI_CLEAR_DOWN   "\033[J"

static void tui_enter()
{
    std::cout << ANSI_ALT_ON << ANSI_HIDE_CUR << std::flush;
#ifndef _WIN32
    enable_raw_mode();
#endif
}

static void tui_leave()
{
#ifndef _WIN32
    disable_raw_mode();
#endif
    std::cout << ANSI_SHOW_CUR << ANSI_ALT_OFF << std::flush;
}

#include <csignal>
static void sigint_handler(int)
{
    tui_leave();
    std::exit(0);
}

// ---------------------------------------------------------------------------
// TUI Menu (with cursor memory)
// ---------------------------------------------------------------------------
struct MenuItem {
    std::string label;
    bool        is_separator = false;
};

// Run a menu, optionally starting at a given cursor index.
// If `initial_cursor` is out of range or points to a separator,
// the first selectable item is used instead.
static int run_menu(const std::string& title, const std::vector<MenuItem>& items,
                    int initial_cursor = -1)
{
    int cursor = 0;
    if (initial_cursor >= 0 && initial_cursor < (int)items.size() &&
        !items[initial_cursor].is_separator)
    {
        cursor = initial_cursor;
    }
    else
    {
        for (int i = 0; i < (int)items.size(); ++i) {
            if (!items[i].is_separator) { cursor = i; break; }
        }
    }

    auto render = [&]() {
        std::cout << ANSI_HOME << ANSI_CLEAR_DOWN;
        std::cout << ANSI_BOLD << "  " << title << ANSI_RESET << "\n\n";
        for (int i = 0; i < (int)items.size(); ++i) {
            if (items[i].is_separator) {
                std::cout << "\n  " << items[i].label << "\n";
                continue;
            }
            if (i == cursor) {
                std::cout << ANSI_REVERSE << " > " << items[i].label
                          << "  " << ANSI_RESET << "\n";
            } else {
                std::cout << "   " << items[i].label << "\n";
            }
        }
        std::cout << std::flush;
    };

    render();

    while (true) {
        int k = read_key();
        if (k == 1000) { // Up
            int prev = cursor;
            do { cursor = (cursor - 1 + (int)items.size()) % (int)items.size(); }
            while (items[cursor].is_separator && cursor != prev);
            render();
        } else if (k == 1001) { // Down
            int prev = cursor;
            do { cursor = (cursor + 1) % (int)items.size(); }
            while (items[cursor].is_separator && cursor != prev);
            render();
        } else if (k == '\r' || k == '\n' || k == ' ') {
            if (!items[cursor].is_separator)
                return cursor;
        } else if (k == 27 || k == 'q' || k == 'Q') {
            for (int i = 0; i < (int)items.size(); ++i) {
                if (!items[i].is_separator &&
                    (items[i].label == "Back" || items[i].label == "Exit"))
                    return i;
            }
        }
    }
}

// Clear any leftover newline (or other characters) from the input buffer
static void clear_input_buffer()
{
    if (std::cin.rdbuf()->in_avail() > 0)
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static std::string prompt_string(const std::string& prompt)
{
#ifndef _WIN32
    disable_raw_mode();
#endif
    std::cout << ANSI_SHOW_CUR;
    clear_input_buffer();                     // discards any pending newline
    std::cout << prompt;
    std::string s;
    std::getline(std::cin, s);
    std::cout << ANSI_HIDE_CUR;
#ifndef _WIN32
    enable_raw_mode();
#endif
    return s;
}

static int prompt_int(const std::string& prompt, int default_val)
{
#ifndef _WIN32
    disable_raw_mode();
#endif
    std::cout << ANSI_SHOW_CUR;
    clear_input_buffer();
    std::cout << prompt << " (default " << default_val << "): ";
    std::string s;
    std::getline(std::cin, s);
    std::cout << ANSI_HIDE_CUR;
#ifndef _WIN32
    enable_raw_mode();
#endif
    if (s.empty()) return default_val;
    int v = std::atoi(s.c_str());
    return v;
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------
static void print_help(const char* prog)
{
    std::cout <<
        "LED Matrix Controller\n"
        "\n"
        "Usage:\n"
        "  " << prog << "                         Interactive TUI menu\n"
        "  " << prog << " -t <text> [options]     Send text directly\n"
        "  " << prog << " -f <file> [options]     Send text file or image\n"
        "  " << prog << " -c [style]              Show digital clock (optional style 0-9, default 0)\n"
        "  " << prog << " --clock-24h             Use 24h format for clock\n"
        "  " << prog << " --clock-date            Show date alternating with time\n"
        "  " << prog << " --save <slot>           Save content to given slot (1-100)\n"
        "  " << prog << " --delete-save <slot>    Delete a single slot\n"
        "  " << prog << " --delete-save ALL       Delete all slots (reset)\n"
        "  " << prog << " -b                      Turn display off (black fill)\n"
        "  " << prog << " -h, --help              Show this help\n"
        "\n"
        "Text options (used with -t or -f for text files):\n"
        "  -scroll-h   -scroll-r   -scroll-v   -fixed   -blink   -breath   -laser\n"
        "  -s <1-255>  Scroll speed (default 80)\n"
        "  -p <1-6>    Font scale (default 1 = 8x10px base glyph)\n"
        "\n"
        "Save options:\n"
        "  --save <slot>            Store the sent image/text/pattern to flash slot (1-100).\n"
        "  --delete-save <slot>     Delete one saved slot.\n"
        "  --delete-save ALL        Clear all slots (reset device).\n"
        "\n"
        "Examples:\n"
        "  " << prog << " -t \"HELLO\" -scroll-h -s 60 -p 1\n"
        "  " << prog << " -t \"CREDITS\" -scroll-v -p 2\n"
        "  " << prog << " -f photo.png\n"
        "  " << prog << " -f animation.gif\n"
        "  " << prog << " -f photo.png --save 1\n"
        "  " << prog << " -c 2 --clock-24h\n"
        "  " << prog << " -b\n"
        "\n"
        "Supported image formats: PNG, JPG/JPEG, BMP, GIF (animated native), PPM\n";
}

// ---------------------------------------------------------------------------
// CLI helpers
// ---------------------------------------------------------------------------
static std::string file_extension(const std::string& path)
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return ext;
}

static bool is_image_extension(const std::string& ext)
{
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".gif" || ext == ".bmp" || ext == ".ppm";
}

static TextOptions parse_text_options(int argc, char* argv[], int start)
{
    TextOptions opts;
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "-scroll-h") opts.effect = TextEffect::ScrollLeft;
        else if (arg == "-scroll-r") opts.effect = TextEffect::ScrollRight;
        else if (arg == "-scroll-v") opts.effect = TextEffect::ScrollUp;
        else if (arg == "-fixed")    opts.effect = TextEffect::Fixed;
        else if (arg == "-blink")    opts.effect = TextEffect::Blinking;
        else if (arg == "-breath")   opts.effect = TextEffect::Breathing;
        else if (arg == "-laser")    opts.effect = TextEffect::Laser;
        else if (arg == "-s" && i + 1 < argc) {
            int v = std::atoi(argv[++i]);
            opts.speed = (uint8_t)(v < 1 ? 1 : v > 255 ? 255 : v);
        } else if (arg == "-p" && i + 1 < argc) {
            int v = std::atoi(argv[++i]);
            opts.font_scale = (uint8_t)(v < 1 ? 1 : v > 6 ? 6 : v);
        }
    }
    return opts;
}

static int parse_save_slot(int argc, char* argv[], int start)
{
    for (int i = start; i < argc; ++i) {
        if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            int slot = std::atoi(argv[i + 1]);
            if (slot < 1 || slot > 100) {
                std::cerr << "[!] Invalid save slot (1-100)\n";
                return -1;
            }
            return slot;
        }
    }
    return 0;
}

static int parse_delete_slot(int argc, char* argv[], int start)
{
    for (int i = start; i < argc; ++i) {
        if (std::strcmp(argv[i], "--delete-save") == 0 && i + 1 < argc) {
            std::string val = argv[i + 1];
            if (val == "ALL" || val == "all") return -1; // all
            int slot = std::atoi(val.c_str());
            if (slot < 1 || slot > 100) {
                std::cerr << "[!] Invalid delete slot (1-100) or ALL\n";
                return -2;
            }
            return slot;
        }
    }
    return 0;
}

static void cli_send_text(const std::string& text, const TextOptions& opts, int save_slot)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        if (save_slot > 0)
            ctrl.save_text(text, (uint8_t)save_slot, {255,255,255}, {0,0,0}, opts);
        else
            ctrl.send_text(text, {255,255,255}, {0,0,0}, opts);
        ctrl.disconnect();
    }
}

static void cli_send_file(const std::string& path, const TextOptions& opts, int save_slot)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    std::string ext = file_extension(path);
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        if (is_image_extension(ext)) {
            if (save_slot > 0)
                ctrl.save_image_file(path, (uint8_t)save_slot, 32, 32);
            else
                ctrl.send_image_file(path, 32, 32);
        } else {
            if (save_slot > 0) {
                std::ifstream f(path);
                if (!f) { std::cerr << "[!] Cannot open text file: " << path << "\n"; continue; }
                std::string full((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
                ctrl.save_text(full, (uint8_t)save_slot, {255,255,255}, {0,0,0}, opts);
            } else {
                ctrl.send_text_file(path, {255,255,255}, {0,0,0}, opts);
            }
        }
        ctrl.disconnect();
    }
}

static void cli_delete_slots(int slot)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        if (slot == -1) ctrl.delete_all_slots();
        else {
            std::vector<uint8_t> slots = { (uint8_t)slot };
            ctrl.delete_slots(slots);
        }
        ctrl.disconnect();
    }
}

static void cli_power_off()
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        ctrl.screen_off();   // black fill
        ctrl.disconnect();
    }
}

static void cli_show_clock(int style, bool is24h, bool show_date)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        ctrl.show_digital_clock((uint8_t)style, is24h, show_date);
        ctrl.disconnect();
    }
}

// ---------------------------------------------------------------------------
// TUI submenus (with cursor memory)
// ---------------------------------------------------------------------------
static TextOptions prompt_text_options_interactive()
{
    TextOptions opts;

    std::vector<MenuItem> items = {
        {"Scroll left (horizontal)"},
        {"Scroll right"},
        {"Scroll up (vertical, end-credits)"},
        {"Fixed"},
        {"Blinking"},
        {"Breathing"},
        {"Laser"},
    };
    int sel = run_menu("Text effect", items);
    switch (sel) {
        case 0: opts.effect = TextEffect::ScrollLeft;  break;
        case 1: opts.effect = TextEffect::ScrollRight; break;
        case 2: opts.effect = TextEffect::ScrollUp;    break;
        case 3: opts.effect = TextEffect::Fixed;       break;
        case 4: opts.effect = TextEffect::Blinking;    break;
        case 5: opts.effect = TextEffect::Breathing;   break;
        case 6: opts.effect = TextEffect::Laser;       break;
    }

    int s = prompt_int("Speed (1-255)", 80);
    opts.speed = (uint8_t)(s < 1 ? 1 : s > 255 ? 255 : s);

    int p = prompt_int("Font scale (1-6, 1=8x10 base)", 1);
    opts.font_scale = (uint8_t)(p < 1 ? 1 : p > 6 ? 6 : p);

    return opts;
}

// pattern selection that remembers the last highlighted item
static bool select_pattern(PatternType& pt, int& cursor)
{
    std::vector<MenuItem> items = {
        {"Checker tiny   (1x1)"},
        {"Checker small  (2x2)"},
        {"Checker medium (3x3)"},
        {"Checker large  (4x4)"},
        {"Horizontal lines"},
        {"Vertical lines"},
        {"Diagonal lines (/ direction)"},
        {"Diagonal lines (\\ direction)"},
        {"Zigzag"},
        {"Grid (lines every 4px)"},
        {"Border square"},
        {"Ring squares (concentric)"},
        {"Crosshair"},
        {"--- Gradients ---", true},
        {"Gradient left -> right"},
        {"Gradient right -> left"},
        {"Gradient top -> bottom"},
        {"Gradient bottom -> top"},
        {"Dots (every 4px)"},
        {"Back"},
    };
    int sel = run_menu("Select pattern", items, cursor);
    cursor = sel;   // remember for next time
    switch (sel) {
        case  0: pt = PatternType::CheckerTiny;     return true;
        case  1: pt = PatternType::CheckerSmall;    return true;
        case  2: pt = PatternType::CheckerMedium;   return true;
        case  3: pt = PatternType::CheckerLarge;    return true;
        case  4: pt = PatternType::HorizontalLines; return true;
        case  5: pt = PatternType::VerticalLines;   return true;
        case  6: pt = PatternType::DiagonalLines;   return true;
        case  7: pt = PatternType::DiagonalBack;    return true;
        case  8: pt = PatternType::Zigzag;          return true;
        case  9: pt = PatternType::Grid;            return true;
        case 10: pt = PatternType::BorderSquare;    return true;
        case 11: pt = PatternType::RingSquares;     return true;
        case 12: pt = PatternType::Crosshair;       return true;
        case 14: pt = PatternType::GradientLeft;    return true;
        case 15: pt = PatternType::GradientRight;   return true;
        case 16: pt = PatternType::GradientDown;    return true;
        case 17: pt = PatternType::GradientUp;      return true;
        case 18: pt = PatternType::Dots;            return true;
        default: return false;   // Back
    }
}

static void do_solid_colour(LEDController& ctrl)
{
    std::vector<MenuItem> items = {
        {"Red"}, {"Green"}, {"Blue"}, {"White"},
        {"Cyan"}, {"Magenta"}, {"Yellow"}, {"Back"}
    };
    int cursor = -1;   // will start at first item
    while (true) {
        int sel = run_menu("Solid colour", items, cursor);
        cursor = sel;   // stay on this item after action
        switch (sel) {
            case 0: ctrl.send_full_frame(0xFF, 0x00, 0x00, 32, 32); break;
            case 1: ctrl.send_full_frame(0x00, 0xFF, 0x00, 32, 32); break;
            case 2: ctrl.send_full_frame(0x00, 0x00, 0xFF, 32, 32); break;
            case 3: ctrl.send_full_frame(0xFF, 0xFF, 0xFF, 32, 32); break;
            case 4: ctrl.send_full_frame(0x00, 0xFF, 0xFF, 32, 32); break;
            case 5: ctrl.send_full_frame(0xFF, 0x00, 0xFF, 32, 32); break;
            case 6: ctrl.send_full_frame(0xFF, 0xFF, 0x00, 32, 32); break;
            default: return;   // Back
        }
    }
}

static void do_save_to_slot(LEDController& ctrl)
{
    int slot = prompt_int("Slot number (1-100)", 1);
    if (slot < 1 || slot > 100) { std::cout << "Invalid slot\n"; return; }
    uint8_t u8slot = (uint8_t)slot;

    std::vector<MenuItem> items = {
        {"Save image from file to slot"},
        {"Save solid colour to slot"},
        {"Save pattern to slot"},
        {"Save text to slot"},
        {"Back"},
    };
    int cursor = -1;
    while (true) {
        int sel = run_menu("Save to slot " + std::to_string(slot), items, cursor);
        cursor = sel;
        if (sel == 0) {
            std::string path = prompt_string("Image file path (PNG/JPG/BMP/GIF/PPM): ");
            if (!path.empty())
                ctrl.save_image_file(path, u8slot, 32, 32);
        } else if (sel == 1) {
            // solid colour submenu – also remembers its own cursor
            std::vector<MenuItem> colours = {
                {"Red"}, {"Green"}, {"Blue"}, {"White"},
                {"Cyan"}, {"Magenta"}, {"Yellow"}, {"Back"}
            };
            int colcur = -1;
            while (true) {
                int csel = run_menu("Save solid colour to slot " + std::to_string(slot), colours, colcur);
                colcur = csel;
                switch (csel) {
                    case 0: ctrl.save_full_frame(0xFF, 0x00, 0x00, u8slot, 32, 32); break;
                    case 1: ctrl.save_full_frame(0x00, 0xFF, 0x00, u8slot, 32, 32); break;
                    case 2: ctrl.save_full_frame(0x00, 0x00, 0xFF, u8slot, 32, 32); break;
                    case 3: ctrl.save_full_frame(0xFF, 0xFF, 0xFF, u8slot, 32, 32); break;
                    case 4: ctrl.save_full_frame(0x00, 0xFF, 0xFF, u8slot, 32, 32); break;
                    case 5: ctrl.save_full_frame(0xFF, 0x00, 0xFF, u8slot, 32, 32); break;
                    case 6: ctrl.save_full_frame(0xFF, 0xFF, 0x00, u8slot, 32, 32); break;
                    default: goto colour_done;
                }
            }
            colour_done:;
        } else if (sel == 2) {
            PatternType pt;
            int patcur = -1;
            while (select_pattern(pt, patcur)) {
                ctrl.save_pattern(pt, u8slot, {255,255,255}, {0,0,0}, 32, 32);
            }
        } else if (sel == 3) {
            std::string msg = prompt_string("Enter text: ");
            if (!msg.empty()) {
                TextOptions opts = prompt_text_options_interactive();
                ctrl.save_text(msg, u8slot, {255,255,255}, {0,0,0}, opts);
            }
        } else {
            // Back
            return;
        }
    }
}

static void do_delete_slots(LEDController& ctrl)
{
    std::vector<MenuItem> items = {
        {"Delete a single slot"},
        {"Delete ALL slots (reset device)"},
        {"Back"},
    };
    int cursor = -1;
    while (true) {
        int sel = run_menu("Delete Slots", items, cursor);
        cursor = sel;
        if (sel == 0) {
            int slot = prompt_int("Slot number (1-100)", 1);
            if (slot >= 1 && slot <= 100) {
                std::vector<uint8_t> slots = { (uint8_t)slot };
                ctrl.delete_slots(slots);
            } else {
                std::cout << "Invalid slot\n";
            }
        } else if (sel == 1) {
            std::cout << "Are you sure? This will erase all saved content! [y/n]: ";
            char c;
            std::cin >> c;
            if (c == 'y' || c == 'Y')
                ctrl.delete_all_slots();
            // clear newline left by operator>>
            if (std::cin.rdbuf()->in_avail() > 0)
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else {
            return;   // Back
        }
    }
}

static void do_digital_clock(LEDController& ctrl)
{
    std::vector<MenuItem> style_items = {
        {"Style 1"}, {"Style 2"}, {"Style 3"}, {"Style 4"}, {"Style 5"},
        {"Style 6"}, {"Style 7"}, {"Style 8"}, {"Style 9"}, {"Style 10"},
        {"Back"}
    };
    int style_sel = run_menu("Clock style", style_items);
    if (style_sel == 10) return;
    uint8_t style = (uint8_t)style_sel;

    std::vector<MenuItem> fmt_items = {{"24-hour"}, {"12-hour"}};
    int fmt_sel = run_menu("Time format", fmt_items);
    bool is24h = (fmt_sel == 0);

    std::vector<MenuItem> date_items = {{"No date"}, {"Show date alternating"}};
    int date_sel = run_menu("Date display", date_items);
    bool show_date = (date_sel == 1);

    ctrl.show_digital_clock(style, is24h, show_date);
}

// ---------------------------------------------------------------------------
// Main interactive loop (cursor preserved across submenus)
// ---------------------------------------------------------------------------
static void interactive_loop(LEDController& ctrl)
{
    std::vector<MenuItem> main_items = {
        {"Solid colour"},
        {"Send text"},
        {"Send image from file"},
        {"Send text from file"},
        {"Send pattern"},
        {"Screen OFF"},
        {"Set brightness"},
        {"Digital clock"},
        {"Save to slot (persists on power-off)"},
        {"Delete slots"},
        {"Exit"},
    };

    int main_cursor = -1;   // remember last main‑menu position
    while (true) {
        int sel = run_menu("LED Matrix Controller", main_items, main_cursor);
        main_cursor = sel;   // stay on this item after any action

        if (sel == 0) {
            do_solid_colour(ctrl);
        } else if (sel == 1) {
            std::string msg = prompt_string("Enter text: ");
            if (!msg.empty()) {
                TextOptions opts = prompt_text_options_interactive();
                ctrl.send_text(msg, {255,255,255}, {0,0,0}, opts);
            }
        } else if (sel == 2) {
            std::string path = prompt_string("Image file path (PNG/JPG/BMP/GIF/PPM): ");
            if (!path.empty())
                ctrl.send_image_file(path, 32, 32);
        } else if (sel == 3) {
            std::string path = prompt_string("Text file path: ");
            if (!path.empty()) {
                TextOptions opts = prompt_text_options_interactive();
                ctrl.send_text_file(path, {255,255,255}, {0,0,0}, opts);
            }
        } else if (sel == 4) {
            PatternType pt;
            int patcur = -1;
            while (select_pattern(pt, patcur)) {
                ctrl.send_pattern(pt, {255,255,255}, {0,0,0}, 32, 32);
            }
        } else if (sel == 5) {
            ctrl.screen_off();
        } else if (sel == 6) {
            int pct = prompt_int("Brightness (1-100)", 100);
            ctrl.set_brightness(pct);
        } else if (sel == 7) {
            do_digital_clock(ctrl);
        } else if (sel == 8) {
            do_save_to_slot(ctrl);
        } else if (sel == 9) {
            do_delete_slots(ctrl);
        } else if (sel == 10) {
            break;   // Exit
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // --delete-save
    {
        int del_slot = parse_delete_slot(argc, argv, 1);
        if (del_slot == -2) return 1;
        if (del_slot != 0) {
            cli_delete_slots(del_slot);
            return 0;
        }
    }

    // -b (power off)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-b") == 0) {
            cli_power_off();
            return 0;
        }
    }

    // -c (digital clock)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-c") == 0) {
            int style = 0;
            if (i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9') {
                style = std::atoi(argv[i+1]);
                if (style < 0 || style > 9) style = 0;
            }
            bool is24h = false, show_date = false;
            for (int j = 1; j < argc; ++j) {
                if (std::strcmp(argv[j], "--clock-24h") == 0) is24h = true;
                if (std::strcmp(argv[j], "--clock-date") == 0) show_date = true;
            }
            cli_show_clock(style, is24h, show_date);
            return 0;
        }
    }

    // -t <text>
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            std::string text = argv[i + 1];
            TextOptions opts = parse_text_options(argc, argv, i + 2);
            int save = parse_save_slot(argc, argv, i + 2);
            if (save < 0) return 1;
            cli_send_text(text, opts, save);
            return 0;
        }
    }

    // -f <file>
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            std::string path = argv[i + 1];
            TextOptions opts = parse_text_options(argc, argv, i + 2);
            int save = parse_save_slot(argc, argv, i + 2);
            if (save < 0) return 1;
            cli_send_file(path, opts, save);
            return 0;
        }
    }

    // Interactive TUI
    LEDController ctrl;
    if (!ctrl.connect()) {
        std::cerr << "[!] Could not connect to any device.\n";
        return 1;
    }

    std::signal(SIGINT, sigint_handler);
    tui_enter();
    interactive_loop(ctrl);
    tui_leave();

    ctrl.disconnect();
    return 0;
}