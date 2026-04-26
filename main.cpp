#include "led_sniffer.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdlib>
#include <cstring>

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
#define ANSI_ALT_ON       "\033[?1049h"   // enter alternate screen buffer
#define ANSI_ALT_OFF      "\033[?1049l"   // leave  alternate screen buffer
#define ANSI_HOME         "\033[H"        // move cursor to top-left
#define ANSI_CLEAR_DOWN   "\033[J"        // erase from cursor to end of screen

// Called once when the interactive session starts.
static void tui_enter()
{
    std::cout << ANSI_ALT_ON << ANSI_HIDE_CUR << std::flush;
#ifndef _WIN32
    enable_raw_mode();
#endif
}

// Called once when the interactive session ends (normal exit or signal).
static void tui_leave()
{
#ifndef _WIN32
    disable_raw_mode();
#endif
    std::cout << ANSI_SHOW_CUR << ANSI_ALT_OFF << std::flush;
}

// SIGINT handler so Ctrl-C also restores the terminal.
#include <csignal>
static void sigint_handler(int)
{
    tui_leave();
    std::exit(0);
}

// ---------------------------------------------------------------------------
// TUI Menu
// ---------------------------------------------------------------------------
struct MenuItem {
    std::string label;
    bool        is_separator = false;
};

// Renders the menu from the top-left of the alternate screen buffer and
// waits for a selection. Returns the index of the chosen item.
// Q / ESC selects the first "Back" or "Exit" item if one exists.
static int run_menu(const std::string& title, const std::vector<MenuItem>& items)
{
    // Find first selectable item
    int cursor = 0;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (!items[i].is_separator) { cursor = i; break; }
    }

    auto render = [&]() {
        // Always redraw from the very top of the alternate buffer.
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

// ---------------------------------------------------------------------------
// Prompt helpers — temporarily leave raw mode so getline works normally
// ---------------------------------------------------------------------------
static std::string prompt_string(const std::string& prompt)
{
#ifndef _WIN32
    disable_raw_mode();
#endif
    std::cout << ANSI_SHOW_CUR;
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
        "  " << prog << " -b                      Black fill (screen off) and exit\n"
        "  " << prog << " -h, --help              Show this help\n"
        "\n"
        "Text options (used with -t or -f):\n"
        "  -scroll-h                Horizontal scroll left (default)\n"
        "  -scroll-v                Vertical scroll up (end-credits style, software rendered)\n"
        "  -scroll-r                Horizontal scroll right\n"
        "  -fixed                   Fixed / static display\n"
        "  -blink                   Blinking effect\n"
        "  -breath                  Breathing effect\n"
        "  -laser                   Laser effect\n"
        "  -s <1-255>               Scroll speed (default 80)\n"
        "  -p <1-6>                 Font scale (default 1 = 8x10px base glyph)\n"
        "                           Scale 2 = 16x20, 3 = 24x30 ... 6 = 48x60\n"
        "\n"
        "Examples:\n"
        "  " << prog << " -t \"HELLO\" -scroll-h -s 60 -p 1\n"
        "  " << prog << " -t \"CREDITS\" -scroll-v -p 2\n"
        "  " << prog << " -f mytext.txt -scroll-h\n"
        "  " << prog << " -f photo.png\n"
        "\n"
        "Supported image formats: PNG, JPG/JPEG, BMP, GIF (first frame), PPM\n";
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

static void cli_send_text(const std::string& text, const TextOptions& opts)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        ctrl.send_text(text, {255,255,255}, {0,0,0}, opts);
        ctrl.disconnect();
    }
}

static void cli_send_file(const std::string& path, const TextOptions& opts)
{
    auto addresses = LEDController::scan_led_addresses();
    if (addresses.empty()) { std::cerr << "[!] No LED devices found.\n"; return; }
    std::string ext = file_extension(path);
    for (const auto& addr : addresses) {
        LEDController ctrl;
        if (!ctrl.connect_silent(addr)) continue;
        if (is_image_extension(ext))
            ctrl.send_image_file(path, 32, 32);
        else
            ctrl.send_text_file(path, {255,255,255}, {0,0,0}, opts);
        ctrl.disconnect();
    }
}

// ---------------------------------------------------------------------------
// Interactive menu helpers
// ---------------------------------------------------------------------------
static TextOptions prompt_text_options_interactive()
{
    TextOptions opts;

    // Effect submenu
    {
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
    }

    int s = prompt_int("Speed (1-255)", 80);
    opts.speed = (uint8_t)(s < 1 ? 1 : s > 255 ? 255 : s);

    int p = prompt_int("Font scale (1-6, 1=8x10 base)", 1);
    opts.font_scale = (uint8_t)(p < 1 ? 1 : p > 6 ? 6 : p);

    return opts;
}

// Maps menu item index -> PatternType (or -1 for Back).
// Returns false for Back, true otherwise (pt is set).
static bool select_pattern(PatternType& pt)
{
    // Keep separator at index 13; real selectable items shift after it.
    // Indices: 0-12 direct, 13=separator(skip), 14-18 gradients, 19=Back
    std::vector<MenuItem> items = {
        {"Checker tiny   (1x1)"},          // 0
        {"Checker small  (2x2)"},          // 1
        {"Checker medium (3x3)"},          // 2
        {"Checker large  (4x4)"},          // 3
        {"Horizontal lines"},              // 4
        {"Vertical lines"},                // 5
        {"Diagonal lines (/ direction)"},  // 6
        {"Diagonal lines (\\ direction)"}, // 7
        {"Zigzag"},                        // 8
        {"Grid (lines every 4px)"},        // 9
        {"Border square"},                 // 10
        {"Ring squares (concentric)"},     // 11
        {"Crosshair"},                     // 12
        {"--- Gradients ---", true},       // 13 separator
        {"Gradient left -> right"},        // 14
        {"Gradient right -> left"},        // 15
        {"Gradient top -> bottom"},        // 16
        {"Gradient bottom -> top"},        // 17
        {"Dots (every 4px)"},              // 18
        {"Back"},                          // 19
    };
    int sel = run_menu("Select pattern", items);
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
        // 13 = separator, run_menu never returns it
        case 14: pt = PatternType::GradientLeft;    return true;
        case 15: pt = PatternType::GradientRight;   return true;
        case 16: pt = PatternType::GradientDown;    return true;
        case 17: pt = PatternType::GradientUp;      return true;
        case 18: pt = PatternType::Dots;            return true;
        default: return false; // Back
    }
}

// ---------------------------------------------------------------------------
// Solid colour submenu
// ---------------------------------------------------------------------------
static void do_solid_colour(LEDController& ctrl)
{
    std::vector<MenuItem> items = {
        {"Red"},
        {"Green"},
        {"Blue"},
        {"White"},
        {"Cyan"},
        {"Magenta"},
        {"Yellow"},
        {"Back"},
    };
    while (true) {
        int sel = run_menu("Solid colour", items);
        switch (sel) {
            case 0: ctrl.send_full_frame(0xFF, 0x00, 0x00, 32, 32); break;
            case 1: ctrl.send_full_frame(0x00, 0xFF, 0x00, 32, 32); break;
            case 2: ctrl.send_full_frame(0x00, 0x00, 0xFF, 32, 32); break;
            case 3: ctrl.send_full_frame(0xFF, 0xFF, 0xFF, 32, 32); break;
            case 4: ctrl.send_full_frame(0x00, 0xFF, 0xFF, 32, 32); break;
            case 5: ctrl.send_full_frame(0xFF, 0x00, 0xFF, 32, 32); break;
            case 6: ctrl.send_full_frame(0xFF, 0xFF, 0x00, 32, 32); break;
            default: return;
        }
    }
}

// ---------------------------------------------------------------------------
// Main interactive loop
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
        {"Exit"},
    };

    while (true) {
        int sel = run_menu("LED Matrix Controller", main_items);

        if (sel == 0) {
            // Solid colour submenu
            do_solid_colour(ctrl);

        } else if (sel == 1) {
            // Send text
            std::string msg = prompt_string("Enter text: ");
            if (!msg.empty()) {
                TextOptions opts = prompt_text_options_interactive();
                ctrl.send_text(msg, {255,255,255}, {0,0,0}, opts);
            }

        } else if (sel == 2) {
            // Image file
            std::string path = prompt_string("Image file path (PNG/JPG/BMP/GIF/PPM): ");
            if (!path.empty())
                ctrl.send_image_file(path, 32, 32);

        } else if (sel == 3) {
            // Text file
            std::string path = prompt_string("Text file path: ");
            if (!path.empty()) {
                TextOptions opts = prompt_text_options_interactive();
                ctrl.send_text_file(path, {255,255,255}, {0,0,0}, opts);
            }

        } else if (sel == 4) {
            // Pattern submenu: stay open until "Back"
            PatternType pt;
            while (select_pattern(pt)) {
                ctrl.send_pattern(pt, {255,255,255}, {0,0,0}, 32, 32);
            }

        } else if (sel == 5) {
            ctrl.screen_off(32, 32);

        } else if (sel == 6) {
            int pct = prompt_int("Brightness (1-100)", 100);
            ctrl.set_brightness(pct);

        } else if (sel == 7) {
            // Exit
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    // Help flag
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    // CLI: -b (black fill / screen off)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-b") == 0) {
            auto addresses = LEDController::scan_led_addresses();
            if (addresses.empty()) {
                std::cerr << "[!] No LED devices found.\n";
                return 1;
            }
            for (const auto& addr : addresses) {
                LEDController ctrl;
                if (!ctrl.connect_silent(addr)) continue;
                ctrl.screen_off(32, 32);
                ctrl.disconnect();
            }
            return 0;
        }
    }

    // CLI: -t <text>
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            std::string text = argv[i + 1];
            TextOptions opts = parse_text_options(argc, argv, i + 2);
            cli_send_text(text, opts);
            return 0;
        }
    }

    // CLI: -f <file>
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            std::string path = argv[i + 1];
            TextOptions opts = parse_text_options(argc, argv, i + 2);
            cli_send_file(path, opts);
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