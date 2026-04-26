#ifndef LED_SNIFFER_H
#define LED_SNIFFER_H

#include <simpleble/SimpleBLE.h>
#include <vector>
#include <string>
#include <atomic>
#include <cstdint>
#include <ctime>
#include "image_loader.h"

struct Color {
    uint8_t r, g, b;
};

enum class TextEffect : uint8_t {
    Fixed       = 0,
    ScrollLeft  = 1,
    ScrollRight = 2,
    Blinking    = 5,
    Breathing   = 6,
    ScrollUp    = 7,   // software vertical scroll
    Laser       = 8,
};

struct TextOptions {
    TextEffect effect     = TextEffect::ScrollLeft;
    uint8_t    speed      = 0x50;
    uint8_t    font_scale = 1;   // 1..6
};

enum class PatternType : uint8_t {
    CheckerTiny, CheckerSmall, CheckerMedium, CheckerLarge,
    HorizontalLines, VerticalLines, DiagonalLines, DiagonalBack,
    Zigzag, Grid, BorderSquare, RingSquares, Crosshair,
    GradientLeft, GradientRight, GradientDown, GradientUp,
    Dots
};

class LEDController {
public:
    bool connect();
    bool connect_silent(const std::string& address);
    static std::vector<std::string> scan_led_addresses();

    // Display a solid colour (no save)
    void send_full_frame(uint8_t r, uint8_t g, uint8_t b,
                         uint8_t width = 32, uint8_t height = 32);

    // Send text with native effect (or software scroll)
    void send_text(const std::string& text,
                   Color fg = {255,255,255},
                   Color bg = {0,0,0},
                   const TextOptions& opts = {});

    // Software vertical scroll (image frames)
    void send_vertical_scroll(const std::string& text,
                               Color fg = {255,255,255},
                               Color bg = {0,0,0},
                               uint8_t speed = 0x50,
                               uint8_t font_scale = 1,
                               uint8_t width = 32,
                               uint8_t height = 32);

    // Send image from file (display only, supports GIF animation)
    void send_image_file(const std::string& path,
                         uint8_t width = 32, uint8_t height = 32);

    // Send text from file (display only)
    void send_text_file(const std::string& path,
                        Color fg = {255,255,255},
                        Color bg = {0,0,0},
                        const TextOptions& opts = {});

    // Send pattern (display only)
    void send_pattern(PatternType type,
                      Color fg = {255,255,255},
                      Color bg = {0,0,0},
                      uint8_t width = 32, uint8_t height = 32);

    // Screen off – sends a black fill (hardware power off not reliable)
    void screen_off();

    // Brightness control
    void set_brightness(int percent);

    // Disconnect
    void disconnect();

    // --- Save-to-slot variants (persist on flash) ---
    void save_full_frame(uint8_t r, uint8_t g, uint8_t b,
                         uint8_t slot, uint8_t width = 32, uint8_t height = 32);
    void save_image_file(const std::string& path,
                         uint8_t slot, uint8_t width = 32, uint8_t height = 32);
    void save_text(const std::string& text,
                   uint8_t slot,
                   Color fg = {255,255,255}, Color bg = {0,0,0},
                   const TextOptions& opts = {});
    void save_pattern(PatternType type,
                      uint8_t slot,
                      Color fg = {255,255,255}, Color bg = {0,0,0},
                      uint8_t width = 32, uint8_t height = 32);

    // --- Slot management ---
    void delete_slots(const std::vector<uint8_t>& slots);
    void delete_all_slots();   // Also resets the display

    // --- Digital Clock ---
    void show_digital_clock(uint8_t style = 0, bool is24h = true,
                            bool show_date = false, time_t when = 0);

private:
    SimpleBLE::Peripheral peripheral_;
    bool connected_        = false;
    bool handshake_primed_ = false;
    float brightness_      = 1.0f;

    // Last solid colour state for brightness repaint
    uint8_t last_r_         = 0;
    uint8_t last_g_         = 0;
    uint8_t last_b_         = 0;
    uint8_t last_width_     = 32;
    uint8_t last_height_    = 32;
    bool    has_last_frame_ = false;

    std::atomic<bool> ack_stage1_{false};
    std::atomic<bool> ack_stage2_{false};
    std::atomic<bool> ack_stage3_{false};

    static constexpr const char* SVC_FA00 = "000000fa-0000-1000-8000-00805f9b34fb";
    static constexpr const char* CHR_FA02 = "0000fa02-0000-1000-8000-00805f9b34fb";
    static constexpr const char* CHR_FA03 = "0000fa03-0000-1000-8000-00805f9b34fb";

    static const std::vector<uint8_t> ACK_S1_A;
    static const std::vector<uint8_t> ACK_S1_B;
    static const std::vector<uint8_t> ACK_S2_A;
    static const std::vector<uint8_t> ACK_S2_B;
    static const std::vector<uint8_t> ACK_S3;

    static std::vector<SimpleBLE::Peripheral> scan_for_led_devices();
    bool connect_to_peripheral(SimpleBLE::Peripheral& dev);

    void send_handshake();
    void send_text_open_sequence(int channel = 3);
    bool wait_ack(std::atomic<bool>& flag, int timeout_ms = 5000);
    void notify_handler(SimpleBLE::ByteArray payload);

    // Low‑level write helpers
    void cmd_write(const std::vector<uint8_t>& data);
    void req_write(const std::vector<uint8_t>& data);

    // Generic raw data sender (handshake + write + ACK)
    void write_raw_data(const std::vector<uint8_t>& data);

    // Send current system time to the panel
    void set_current_time();

    // PNG encoder
    static std::vector<uint8_t> build_png_from_rgb(const uint8_t* rgb, uint8_t width, uint8_t height);

    // Build BLE frame for a PNG image
    static std::vector<uint8_t> build_image_frame(const std::vector<uint8_t>& png,
                                                  uint8_t save_slot = 0x65);

    // Build BLE frame for a GIF (command 0x03,0x00)
    static std::vector<uint8_t> build_gif_frame(const std::vector<uint8_t>& gif_data,
                                                uint8_t save_slot = 0x65);

    // Build BLE packet for native text
    static std::vector<uint8_t> build_text_payload(const std::string& text,
                                                   Color fg, Color bg,
                                                   uint8_t effect_code, uint8_t speed, uint8_t font_scale,
                                                   uint8_t save_slot = 0x65);

    // Glyph helpers
    static uint8_t              reverse_bits(uint8_t v);
    static std::vector<uint8_t> glyph_for_char(char ch, uint8_t scale = 1);

    // Software scroll frame builder
    static std::vector<uint8_t> build_vertical_scroll_frame(
        const std::vector<std::vector<uint8_t>>& glyph_strip,
        int glyph_w, int glyph_h,
        int offset_y,
        Color fg, Color bg,
        uint8_t width, uint8_t height);

    // Text total data (for native text command)
    static std::vector<uint8_t> build_text_total_data(
        const std::string& text, Color fg, Color bg,
        uint8_t effect_code, uint8_t speed, uint8_t font_scale);

    // Pattern generator
    static std::vector<uint8_t> build_pattern_rgb(
        PatternType type, Color fg, Color bg, uint8_t width, uint8_t height);
};

#endif