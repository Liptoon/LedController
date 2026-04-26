#include "led_sniffer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <zlib.h>

// ---------------------------------------------------------------------------
// Known ACK byte sequences
// ---------------------------------------------------------------------------
const std::vector<uint8_t> LEDController::ACK_S1_A = {0x0C,0x00,0x01,0x80,0x81,0x06,0x32,0x00,0x00,0x01,0x00,0x01};
const std::vector<uint8_t> LEDController::ACK_S1_B = {0x0B,0x00,0x01,0x80,0x83,0x06,0x32,0x00,0x00,0x01,0x00};
const std::vector<uint8_t> LEDController::ACK_S2_A = {0x08,0x00,0x05,0x80,0x0B,0x03,0x07,0x02};
const std::vector<uint8_t> LEDController::ACK_S2_B = {0x08,0x00,0x05,0x80,0x0E,0x03,0x07,0x01};
const std::vector<uint8_t> LEDController::ACK_S3   = {0x05,0x00,0x02,0x00,0x03};

// ---------------------------------------------------------------------------
// Minimal PNG encoder
// ---------------------------------------------------------------------------
static void png_write_chunk(std::vector<uint8_t>& out,
                             const char tag[4],
                             const uint8_t* data, uint32_t len)
{
    out.push_back((len >> 24) & 0xFF);
    out.push_back((len >> 16) & 0xFF);
    out.push_back((len >>  8) & 0xFF);
    out.push_back( len        & 0xFF);
    out.insert(out.end(), tag, tag + 4);
    if (data && len > 0)
        out.insert(out.end(), data, data + len);
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, (const Bytef*)tag, 4);
    if (data && len > 0)
        crc = (uint32_t)crc32(crc, (const Bytef*)data, len);
    out.push_back((crc >> 24) & 0xFF);
    out.push_back((crc >> 16) & 0xFF);
    out.push_back((crc >>  8) & 0xFF);
    out.push_back( crc        & 0xFF);
}

std::vector<uint8_t> LEDController::build_png_from_rgb(
        const uint8_t* rgb, uint8_t width, uint8_t height)
{
    std::vector<uint8_t> out;
    out.reserve(256 + width * height * 3);

    const uint8_t sig[8] = {0x89,'P','N','G','\r','\n',0x1A,'\n'};
    out.insert(out.end(), sig, sig + 8);

    uint8_t ihdr[13] = {};
    ihdr[3] = width;
    ihdr[7] = height;
    ihdr[8] = 8;
    ihdr[9] = 2;
    png_write_chunk(out, "IHDR", ihdr, 13);

    std::vector<uint8_t> raw;
    raw.reserve((1 + width * 3) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0x00);
        raw.insert(raw.end(),
                   rgb + y * width * 3,
                   rgb + y * width * 3 + width * 3);
    }

    uLongf csize = compressBound((uLong)raw.size());
    std::vector<uint8_t> compressed(csize);
    compress2(compressed.data(), &csize,
              raw.data(), (uLong)raw.size(), Z_DEFAULT_COMPRESSION);
    compressed.resize(csize);
    png_write_chunk(out, "IDAT", compressed.data(), (uint32_t)csize);
    png_write_chunk(out, "IEND", nullptr, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Build BLE frame for a PNG image (Show Image command)
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_image_frame(const std::vector<uint8_t>& png,
                                                      uint8_t save_slot)
{
    uint32_t data_len  = (uint32_t)png.size();
    uint32_t total_len = data_len + 15;
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, png.data(), data_len);

    std::vector<uint8_t> frame;
    frame.reserve(total_len);

    frame.push_back( total_len        & 0xFF);
    frame.push_back((total_len >>  8) & 0xFF);
    frame.push_back(0x02);            // command ID 0x0002 (raw image)
    frame.push_back(0x00);
    frame.push_back(0x00);            // has_next_chunk
    frame.push_back( data_len        & 0xFF);
    frame.push_back((data_len >>  8) & 0xFF);
    frame.push_back((data_len >> 16) & 0xFF);
    frame.push_back((data_len >> 24) & 0xFF);
    frame.push_back( crc        & 0xFF);
    frame.push_back((crc >>  8) & 0xFF);
    frame.push_back((crc >> 16) & 0xFF);
    frame.push_back((crc >> 24) & 0xFF);
    frame.push_back(0x00);            // type_byte (raw)
    frame.push_back(save_slot);
    frame.insert(frame.end(), png.begin(), png.end());
    return frame;
}

// ---------------------------------------------------------------------------
// Build BLE frame for a GIF (Show Image command – GIF variant)
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_gif_frame(const std::vector<uint8_t>& gif_data,
                                                    uint8_t save_slot)
{
    uint32_t data_len  = (uint32_t)gif_data.size();
    uint32_t total_len = data_len + 15;
    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, gif_data.data(), data_len);

    std::vector<uint8_t> frame;
    frame.reserve(total_len);

    frame.push_back( total_len        & 0xFF);
    frame.push_back((total_len >>  8) & 0xFF);
    frame.push_back(0x03);            // command ID 0x0003 (GIF)
    frame.push_back(0x00);
    frame.push_back(0x00);            // has_next_chunk
    frame.push_back( data_len        & 0xFF);
    frame.push_back((data_len >>  8) & 0xFF);
    frame.push_back((data_len >> 16) & 0xFF);
    frame.push_back((data_len >> 24) & 0xFF);
    frame.push_back( crc        & 0xFF);
    frame.push_back((crc >>  8) & 0xFF);
    frame.push_back((crc >> 16) & 0xFF);
    frame.push_back((crc >> 24) & 0xFF);
    frame.push_back(0x02);            // type_byte (GIF)
    frame.push_back(save_slot);
    frame.insert(frame.end(), gif_data.begin(), gif_data.end());
    return frame;
}

// ---------------------------------------------------------------------------
// Build BLE packet for native text
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_text_payload(
    const std::string& text, Color fg, Color bg,
    uint8_t effect_code, uint8_t speed, uint8_t font_scale,
    uint8_t save_slot)
{
    auto total_data = build_text_total_data(text, fg, bg, effect_code, speed, font_scale);
    uint32_t data_len = (uint32_t)total_data.size();
    uint32_t pkt_len  = data_len + 15;

    uint32_t crc = (uint32_t)crc32(0L, Z_NULL, 0);
    crc = (uint32_t)crc32(crc, total_data.data(), data_len);

    std::vector<uint8_t> pkt;
    pkt.reserve(pkt_len);

    pkt.push_back( pkt_len        & 0xFF);
    pkt.push_back((pkt_len >>  8) & 0xFF);
    pkt.push_back(0x00);
    pkt.push_back(0x01);
    pkt.push_back(0x00);
    pkt.push_back( data_len        & 0xFF);
    pkt.push_back((data_len >>  8) & 0xFF);
    pkt.push_back((data_len >> 16) & 0xFF);
    pkt.push_back((data_len >> 24) & 0xFF);
    pkt.push_back( crc        & 0xFF);
    pkt.push_back((crc >>  8) & 0xFF);
    pkt.push_back((crc >> 16) & 0xFF);
    pkt.push_back((crc >> 24) & 0xFF);
    pkt.push_back(0x00);
    pkt.push_back(save_slot);
    pkt.insert(pkt.end(), total_data.begin(), total_data.end());
    return pkt;
}

// ---------------------------------------------------------------------------
// Notification handler
// ---------------------------------------------------------------------------
void LEDController::notify_handler(SimpleBLE::ByteArray payload)
{
    std::vector<uint8_t> p(payload.begin(), payload.end());
    if (p == ACK_S1_A || p == ACK_S1_B) { ack_stage1_ = true; return; }
    if (p == ACK_S2_A || p == ACK_S2_B) { ack_stage2_ = true; return; }
    if (p == ACK_S3)                     { ack_stage3_ = true; return; }
}

bool LEDController::wait_ack(std::atomic<bool>& flag, int timeout_ms)
{
    const int step_ms = 10;
    for (int t = 0; t < timeout_ms; t += step_ms) {
        if (flag.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
    return false;
}

void LEDController::cmd_write(const std::vector<uint8_t>& data)
{
    SimpleBLE::ByteArray ba(data.begin(), data.end());
    try {
        peripheral_.write_command(SVC_FA00, CHR_FA02, ba);
    } catch (const std::exception& e) {
        std::cerr << "[!] cmd_write: " << e.what() << "\n";
    }
}

void LEDController::req_write(const std::vector<uint8_t>& data)
{
    SimpleBLE::ByteArray ba(data.begin(), data.end());
    try {
        peripheral_.write_request(SVC_FA00, CHR_FA02, ba);
    } catch (const std::exception& e) {
        std::cerr << "[!] req_write: " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// Scan & connect
// ---------------------------------------------------------------------------
std::vector<SimpleBLE::Peripheral> LEDController::scan_for_led_devices()
{
    auto adapters = SimpleBLE::Adapter::get_adapters();
    if (adapters.empty()) {
        std::cerr << "[!] No BLE adapters found\n";
        return {};
    }

    std::cout << "[...] Scanning for 5 seconds...\n";
    auto adapter = adapters[0];
    adapter.scan_for(5000);
    auto all = adapter.scan_get_results();

    std::vector<SimpleBLE::Peripheral> found;
    for (auto& dev : all)
        if (dev.identifier().find("LED") != std::string::npos)
            found.push_back(dev);
    return found;
}

std::vector<std::string> LEDController::scan_led_addresses()
{
    auto devices = scan_for_led_devices();
    std::vector<std::string> addresses;
    addresses.reserve(devices.size());
    for (auto& dev : devices)
        addresses.push_back(dev.address());
    return addresses;
}

bool LEDController::connect_to_peripheral(SimpleBLE::Peripheral& dev)
{
    peripheral_ = dev;
    peripheral_.connect();
    if (!peripheral_.is_connected()) {
        std::cerr << "[!] Connection failed: " << dev.address() << "\n";
        return false;
    }
    peripheral_.notify(SVC_FA00, CHR_FA03,
        [this](SimpleBLE::ByteArray p) { notify_handler(p); });
    connected_ = true;
    handshake_primed_ = false;
    return true;
}

bool LEDController::connect()
{
    auto devices = scan_for_led_devices();
    if (devices.empty()) {
        std::cerr << "[!] No LED devices found in scan\n";
        return false;
    }

    std::cout << "\nFound " << devices.size() << " device(s):\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << (i + 1) << "] "
                  << devices[i].identifier()
                  << "  " << devices[i].address() << "\n";
    }

    size_t choice = 0;
    if (devices.size() == 1) {
        std::cout << "Connect to [1] " << devices[0].identifier()
                  << " (" << devices[0].address() << ")? [y/n]: ";
        char c;
        std::cin >> c;
        if (c != 'y' && c != 'Y') { std::cout << "Aborted.\n"; return false; }
    } else {
        std::cout << "Enter number to connect (0 to abort): ";
        int n;
        std::cin >> n;
        if (n <= 0 || n > (int)devices.size()) { std::cout << "Aborted.\n"; return false; }
        choice = (size_t)(n - 1);
    }

    std::cout << "[...] Connecting to " << devices[choice].identifier()
              << " (" << devices[choice].address() << ")...\n";
    if (!connect_to_peripheral(devices[choice]))
        return false;

    std::cout << "[OK] Connected: " << peripheral_.identifier()
              << "  " << peripheral_.address() << "\n";
    return true;
}

bool LEDController::connect_silent(const std::string& address)
{
    auto devices = scan_for_led_devices();
    for (auto& dev : devices) {
        if (dev.address() == address) {
            if (!connect_to_peripheral(dev))
                return false;
            std::cout << "[OK] " << address << "\n";
            return true;
        }
    }
    std::cerr << "[!] Device not found: " << address << "\n";
    return false;
}

// ---------------------------------------------------------------------------
// Screen off – sends a black fill (reliable way to blank the display)
// ---------------------------------------------------------------------------
void LEDController::screen_off()
{
    send_full_frame(0x00, 0x00, 0x00, 32, 32);
    std::cout << "[INFO] Screen blanked\n";
}
// ---------------------------------------------------------------------------
// Send current system time to the panel
// ---------------------------------------------------------------------------
void LEDController::set_current_time()
{
    if (!connected_) return;
    std::time_t now = std::time(nullptr);
    struct tm *lt = std::localtime(&now);

    std::vector<uint8_t> cmd = {
        0x08, 0x00,               // length
        0x01, 0x80,               // set_time command
        (uint8_t)lt->tm_hour,
        (uint8_t)lt->tm_min,
        (uint8_t)lt->tm_sec,
        0x00                      // language (English)
    };
    cmd_write(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
}
// ---------------------------------------------------------------------------
// Digital Clock
// ---------------------------------------------------------------------------
void LEDController::show_digital_clock(uint8_t style, bool is24h,
                                        bool show_date, time_t when)
{
    if (!connected_) return;

    // Sync panel's internal clock with current system time
    set_current_time();

    if (when == 0) when = std::time(nullptr);
    struct tm *lt = std::localtime(&when);
    int year = lt->tm_year + 1900 - 2000;
    uint8_t month = (uint8_t)(lt->tm_mon + 1);
    uint8_t day   = (uint8_t)lt->tm_mday;
    uint8_t dow = (lt->tm_wday == 0) ? 7 : (uint8_t)lt->tm_wday;   // 7 = Sunday

    std::vector<uint8_t> cmd;
    cmd.reserve(11);
    cmd.push_back(0x0B);  // length low
    cmd.push_back(0x00);  // length high
    cmd.push_back(0x06);  // command 0x0106
    cmd.push_back(0x01);
    cmd.push_back(style);
    cmd.push_back(is24h ? 1 : 0);
    cmd.push_back(show_date ? 1 : 0);
    cmd.push_back((uint8_t)year);
    cmd.push_back(month);
    cmd.push_back(day);
    cmd.push_back(dow);

    std::cout << "[INFO] Setting digital clock style=" << (int)style
              << " 24h=" << (is24h ? "yes" : "no")
              << " show_date=" << (show_date ? "yes" : "no") << "\n";
    cmd_write(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------
void LEDController::send_handshake()
{
    const std::vector<uint8_t> hs1 = {0x08,0x00,0x01,0x80,0x0E,0x06,0x32,0x00};
    const std::vector<uint8_t> hs2 = {0x04,0x00,0x05,0x80};

    ack_stage1_ = false;
    ack_stage2_ = false;
    ack_stage3_ = false;

    cmd_write(hs1);
    if (!wait_ack(ack_stage1_, 5000))
        std::cerr << "[!] Handshake stage-1 timeout\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ack_stage2_ = false;
    cmd_write(hs2);
    wait_ack(ack_stage2_, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    handshake_primed_ = true;
}

// ---------------------------------------------------------------------------
// Text open sequence
// ---------------------------------------------------------------------------
void LEDController::send_text_open_sequence(int channel)
{
    cmd_write({0x08, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00});
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cmd_write({0x04, 0x00, 0x05, 0x80});
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cmd_write({0x05, 0x00, 0x12, 0x80, 0x07});
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    cmd_write({0x07, 0x00, 0x08, 0x80, 0x01, 0x00,
               static_cast<uint8_t>(channel & 0xFF)});
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    handshake_primed_ = false;
}

// ---------------------------------------------------------------------------
// Glyph table & helpers
// ---------------------------------------------------------------------------
static const uint8_t GLYPH_DATA[][10] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x1C,0x36,0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x63}, // A
    {0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC}, // B
    {0x3C,0x66,0x63,0x60,0x60,0x60,0x60,0x63,0x66,0x3C}, // C
    {0x3E,0x66,0x63,0x63,0x63,0x63,0x63,0x63,0x66,0x3E}, // D
    {0x7F,0x60,0x60,0x60,0x7E,0x60,0x60,0x60,0x60,0x7F}, // E
    {0x7F,0x60,0x60,0x60,0x7E,0x60,0x60,0x60,0x60,0x60}, // F
    {0x3C,0x66,0x63,0x60,0x60,0x6F,0x63,0x63,0x66,0x3C}, // G
    {0x63,0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x63,0x63}, // H
    {0x3E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3E}, // I
    {0x1F,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C}, // J
    {0x63,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x63,0x63}, // K
    {0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7F}, // L
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x63,0x63,0x63}, // M
    {0x63,0x73,0x7B,0x7F,0x6F,0x67,0x63,0x63,0x63,0x63}, // N
    {0x3E,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E}, // O
    {0x3F,0x66,0x66,0x66,0x3E,0x60,0x60,0x60,0x60,0x60}, // P
    {0x3E,0x63,0x63,0x63,0x63,0x63,0x6B,0x67,0x3E,0x03}, // Q
    {0x3F,0x66,0x66,0x66,0x3E,0x78,0x6C,0x66,0x63,0x63}, // R
    {0x3E,0x63,0x60,0x60,0x3E,0x03,0x03,0x03,0x63,0x3E}, // S
    {0x7F,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C}, // T
    {0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E}, // U
    {0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x36,0x1C,0x08}, // V
    {0x63,0x63,0x63,0x63,0x63,0x6B,0x7F,0x7F,0x77,0x63}, // W
    {0x63,0x63,0x36,0x1C,0x08,0x1C,0x36,0x63,0x63,0x63}, // X
    {0x63,0x63,0x63,0x36,0x1C,0x0C,0x0C,0x0C,0x0C,0x0C}, // Y
    {0x7F,0x03,0x06,0x0C,0x18,0x30,0x60,0x60,0x60,0x7F}, // Z
    {0x3E,0x63,0x63,0x67,0x6B,0x73,0x63,0x63,0x63,0x3E}, // 0
    {0x0C,0x1C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3E}, // 1
    {0x3E,0x63,0x03,0x03,0x06,0x0C,0x18,0x30,0x60,0x7F}, // 2
    {0x3E,0x63,0x03,0x03,0x1E,0x03,0x03,0x03,0x63,0x3E}, // 3
    {0x06,0x0E,0x1E,0x36,0x66,0x66,0x7F,0x06,0x06,0x06}, // 4
    {0x7F,0x60,0x60,0x60,0x7E,0x03,0x03,0x03,0x63,0x3E}, // 5
    {0x1E,0x30,0x60,0x60,0x7E,0x63,0x63,0x63,0x63,0x3E}, // 6
    {0x7F,0x03,0x03,0x06,0x06,0x0C,0x0C,0x18,0x18,0x18}, // 7
    {0x3E,0x63,0x63,0x63,0x3E,0x63,0x63,0x63,0x63,0x3E}, // 8
    {0x3E,0x63,0x63,0x63,0x3F,0x03,0x03,0x06,0x0C,0x38}, // 9
    {0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x00,0x0C,0x00}, // !
    {0x3E,0x63,0x03,0x03,0x0E,0x0C,0x0C,0x00,0x0C,0x00}, // ?
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x00}, // .
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
    {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x18,0x00,0x00,0x00,0x18,0x00,0x00,0x00}, // :
    {0x01,0x03,0x06,0x06,0x0C,0x18,0x18,0x30,0x60,0x40}, // /
};

static int glyph_index(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return 1 + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return 1 + (ch - 'a');
    if (ch >= '0' && ch <= '9') return 27 + (ch - '0');
    switch (ch) {
        case '!': return 37;
        case '?': return 38;
        case '.': return 39;
        case ',': return 40;
        case '-': return 41;
        case ':': return 42;
        case '/': return 43;
        default:  return 0;
    }
}

uint8_t LEDController::reverse_bits(uint8_t v)
{
    uint8_t out = 0;
    for (int i = 0; i < 8; ++i) { out = (out << 1) | (v & 1); v >>= 1; }
    return out;
}

std::vector<uint8_t> LEDController::glyph_for_char(char ch, uint8_t scale)
{
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;

    const uint8_t* src = GLYPH_DATA[glyph_index(ch)];

    if (scale == 1) {
        std::vector<uint8_t> out(10);
        for (int i = 0; i < 10; ++i) out[i] = reverse_bits(src[i]);
        return out;
    }

    const int scaled_rows = 10 * scale;
    const int bytes_per_row = scale;
    std::vector<uint8_t> out(scaled_rows * bytes_per_row, 0x00);

    for (int src_row = 0; src_row < 10; ++src_row) {
        uint8_t src_byte = src[src_row];
        uint8_t row_bytes[6] = {};
        for (int src_bit = 0; src_bit < 8; ++src_bit) {
            int pixel_on = (src_byte >> (7 - src_bit)) & 1;
            for (int rep = 0; rep < scale; ++rep) {
                int dst_bit_index = src_bit * scale + rep;
                int dst_byte      = dst_bit_index / 8;
                int dst_bit       = 7 - (dst_bit_index % 8);
                if (pixel_on && dst_byte < bytes_per_row)
                    row_bytes[dst_byte] |= (1 << dst_bit);
            }
        }
        uint8_t rev[6] = {};
        for (int b = 0; b < bytes_per_row; ++b)
            rev[b] = reverse_bits(row_bytes[b]);

        for (int row_rep = 0; row_rep < scale; ++row_rep) {
            int dst_row = src_row * scale + row_rep;
            for (int b = 0; b < bytes_per_row; ++b)
                out[dst_row * bytes_per_row + b] = rev[b];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// build_vertical_scroll_frame
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_vertical_scroll_frame(
    const std::vector<std::vector<uint8_t>>& glyph_strip,
    int glyph_w, int glyph_h,
    int offset_y,
    Color fg, Color bg,
    uint8_t width, uint8_t height)
{
    const int slot_h = glyph_h + 2;
    const int strip_total = (int)glyph_strip.size() * slot_h;
    const int bytes_per_glyph_row = (glyph_w + 7) / 8;
    const int x_start = (width - glyph_w) / 2;

    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        rgb[i*3+0] = bg.r; rgb[i*3+1] = bg.g; rgb[i*3+2] = bg.b;
    }

    for (int screen_y = 0; screen_y < height; ++screen_y) {
        int strip_y = ((offset_y + screen_y) % strip_total + strip_total) % strip_total;
        int glyph_idx = strip_y / slot_h;
        int row_in_slot = strip_y % slot_h;

        if (row_in_slot >= glyph_h) continue;
        if (glyph_idx >= (int)glyph_strip.size()) continue;

        const auto& glyph = glyph_strip[glyph_idx];
        int row_byte_offset = row_in_slot * bytes_per_glyph_row;

        for (int gx = 0; gx < glyph_w; ++gx) {
            int screen_x = x_start + gx;
            if (screen_x < 0 || screen_x >= width) continue;

            int byte_idx = row_byte_offset + gx / 8;
            int bit_pos  = 7 - (gx % 8);
            bool lit = false;
            if (byte_idx < (int)glyph.size())
                lit = (glyph[byte_idx] >> bit_pos) & 1;

            int pixel = (screen_y * width + screen_x) * 3;
            if (lit) {
                rgb[pixel+0] = fg.r; rgb[pixel+1] = fg.g; rgb[pixel+2] = fg.b;
            }
        }
    }
    return rgb;
}

// ---------------------------------------------------------------------------
// send_vertical_scroll
// ---------------------------------------------------------------------------
void LEDController::send_vertical_scroll(const std::string& text,
                                          Color fg, Color bg,
                                          uint8_t speed,
                                          uint8_t font_scale,
                                          uint8_t width,
                                          uint8_t height)
{
    if (!connected_) return;
    if (font_scale < 1) font_scale = 1;
    if (font_scale > 6) font_scale = 6;

    std::string t = text.empty() ? " " : text;

    const int glyph_w = 8 * font_scale;
    const int glyph_h = 10 * font_scale;
    const int slot_h  = glyph_h + 2 * font_scale;

    std::vector<std::vector<uint8_t>> strip;
    strip.reserve(t.size());
    for (char ch : t)
        strip.push_back(glyph_for_char(ch, font_scale));

    const int strip_total = (int)strip.size() * slot_h;
    int ms_per_pixel = std::max(5, 200 - (int)speed);

    std::cout << "[INFO] Vertical scroll: " << t.size() << " chars, "
              << glyph_w << "x" << glyph_h << " glyphs, scale=" << (int)font_scale
              << ", strip_h=" << strip_total << "px\n";

    const int total_scroll = strip_total + height;

    for (int offset = 0; offset < total_scroll; ++offset) {
        int offset_y = offset - height;

        auto rgb = build_vertical_scroll_frame(strip, glyph_w, slot_h,
                                                offset_y, fg, bg, width, height);

        for (auto& ch : rgb) {
            int v = static_cast<int>(ch * brightness_);
            ch = static_cast<uint8_t>(v > 255 ? 255 : v);
        }

        auto png   = build_png_from_rgb(rgb.data(), width, height);
        auto frame = build_image_frame(png, 0x65);
        write_raw_data(frame);

        std::this_thread::sleep_for(std::chrono::milliseconds(ms_per_pixel));
    }

    std::cout << "[OK] Vertical scroll done\n";
}

// ---------------------------------------------------------------------------
// build_text_total_data
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_text_total_data(
    const std::string& text, Color fg, Color bg,
    uint8_t effect_code, uint8_t speed, uint8_t font_scale)
{
    if (font_scale < 1) font_scale = 1;
    if (font_scale > 6) font_scale = 6;

    std::string t = text.empty() ? " " : text;
    if (t.size() > 255) t = t.substr(0, 255);
    int n = (int)t.size();

    const int glyph_bytes = font_scale * font_scale * 10;

    std::vector<uint8_t> body;
    body.reserve(6 * n + n * glyph_bytes + 21);

    body.push_back((uint8_t)n);
    body.push_back(0x00);
    body.push_back(0x01);
    body.push_back(0x01);
    body.push_back(effect_code);
    body.push_back(speed);
    body.push_back(0x00);
    body.push_back(fg.r); body.push_back(fg.g); body.push_back(fg.b);
    body.push_back(bg.r); body.push_back(bg.g); body.push_back(bg.b);
    body.push_back(0x00); body.push_back(0x00);
    body.push_back(fg.r); body.push_back(fg.g); body.push_back(fg.b);
    body.push_back(bg.r); body.push_back(bg.g); body.push_back(bg.b);

    auto g0 = glyph_for_char(t[0], font_scale);
    body.insert(body.end(), g0.begin(), g0.end());
    int sep0 = (n > 1) ? 4 : 3;
    for (int i = 0; i < sep0; ++i) body.push_back(0x00);

    for (int idx = 1; idx < n; ++idx) {
        body.push_back(fg.r); body.push_back(fg.g); body.push_back(fg.b);
        body.push_back(bg.r); body.push_back(bg.g); body.push_back(bg.b);
        auto g = glyph_for_char(t[idx], font_scale);
        body.insert(body.end(), g.begin(), g.end());
        int sep = (idx < n - 1) ? 4 : 3;
        for (int i = 0; i < sep; ++i) body.push_back(0x00);
    }
    return body;
}

// ---------------------------------------------------------------------------
// send_text
// ---------------------------------------------------------------------------
void LEDController::send_text(const std::string& text,
                               Color fg, Color bg, const TextOptions& opts)
{
    if (!connected_) return;

    if (opts.effect == TextEffect::ScrollUp) {
        send_vertical_scroll(text, fg, bg, opts.speed, opts.font_scale, 32, 32);
        return;
    }

    const int CHUNK_SIZE  = 509;
    const int INTERVAL_MS = 60;

    uint8_t effect_code = static_cast<uint8_t>(opts.effect);
    uint8_t speed       = opts.speed < 1 ? 1 : opts.speed;
    uint8_t font_scale  = opts.font_scale < 1 ? 1 : (opts.font_scale > 6 ? 6 : opts.font_scale);

    send_text_open_sequence(3);

    auto payload = build_text_payload(text, fg, bg, effect_code, speed, font_scale, 0x65);
    std::cout << "[INFO] text=\"" << text
              << "\" effect=" << (int)effect_code
              << " speed=" << (int)speed
              << " scale=" << (int)font_scale
              << " payload=" << payload.size() << "B\n";

    for (size_t offset = 0; offset < payload.size(); offset += CHUNK_SIZE) {
        size_t len = std::min<size_t>(CHUNK_SIZE, payload.size() - offset);
        std::vector<uint8_t> chunk(payload.begin() + offset,
                                   payload.begin() + offset + len);
        cmd_write(chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_MS));
    }

    std::cout << "[OK] Text sent\n";
}

// ---------------------------------------------------------------------------
// send_full_frame (display only)
// ---------------------------------------------------------------------------
void LEDController::send_full_frame(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t width, uint8_t height)
{
    if (!connected_) return;

    last_r_ = r; last_g_ = g; last_b_ = b;
    last_width_ = width; last_height_ = height;
    has_last_frame_ = true;

    auto scale = [&](uint8_t ch) -> uint8_t {
        int v = static_cast<int>(ch * brightness_);
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    uint8_t sr = scale(r), sg = scale(g), sb = scale(b);

    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        rgb[i*3+0] = sr; rgb[i*3+1] = sg; rgb[i*3+2] = sb;
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, 0x65);

    std::cout << "[INFO] Sending " << (int)width << "x" << (int)height
              << " brightness=" << static_cast<int>(brightness_ * 100.0f + 0.5f) << "%"
              << " png=" << png.size() << "B  frame=" << frame.size() << "B\n";

    write_raw_data(frame);
}

// ---------------------------------------------------------------------------
// save_full_frame (save to slot)
// ---------------------------------------------------------------------------
void LEDController::save_full_frame(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t slot, uint8_t width, uint8_t height)
{
    if (!connected_) return;
    if (slot < 1 || slot > 100) {
        std::cerr << "[!] Invalid slot number (1-100)\n";
        return;
    }

    auto scale = [&](uint8_t ch) -> uint8_t {
        int v = static_cast<int>(ch * brightness_);
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    uint8_t sr = scale(r), sg = scale(g), sb = scale(b);

    std::vector<uint8_t> rgb(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        rgb[i*3+0] = sr; rgb[i*3+1] = sg; rgb[i*3+2] = sb;
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, slot);

    std::cout << "[INFO] Saving solid colour " << (int)width << "x" << (int)height
              << " to slot " << (int)slot << "  png=" << png.size()
              << "B  frame=" << frame.size() << "B\n";

    write_raw_data(frame);
    std::cout << "[OK] Frame saved to device flash slot " << (int)slot << "\n";
}

// ---------------------------------------------------------------------------
// write_raw_data
// ---------------------------------------------------------------------------
void LEDController::write_raw_data(const std::vector<uint8_t>& data)
{
    if (!connected_) return;

    if (!handshake_primed_)
        send_handshake();

    ack_stage3_ = false;
    req_write(data);

    if (!wait_ack(ack_stage3_, 5000))
        std::cerr << "[!] Frame ACK timeout\n";
    else
        std::cout << "[OK] Frame ACK received\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------
void LEDController::disconnect()
{
    if (connected_) {
        try { peripheral_.unsubscribe(SVC_FA00, CHR_FA03); } catch (...) {}
        peripheral_.disconnect();
        connected_ = false;
        handshake_primed_ = false;
    }
}

// ---------------------------------------------------------------------------
// send_image_file (display only, GIF aware)
// ---------------------------------------------------------------------------
void LEDController::send_image_file(const std::string& path,
                                     uint8_t width, uint8_t height)
{
    if (!connected_) return;

    // Check extension for GIF
    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
    }

    if (ext == ".gif") {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "[!] Cannot open GIF file: " << path << "\n";
            return;
        }
        std::vector<uint8_t> gif_data((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        auto frame = build_gif_frame(gif_data, 0x65);
        std::cout << "[INFO] Sending animated GIF (" << gif_data.size() << " bytes)\n";
        write_raw_data(frame);
        return;
    }

    // Standard image
    std::cout << "[INFO] Loading image: " << path << "\n";
    auto rgb = load_image_for_panel(path, (int)width, (int)height);
    if (rgb.empty()) return;

    for (auto& ch : rgb) {
        int v = static_cast<int>(ch * brightness_);
        ch = static_cast<uint8_t>(v > 255 ? 255 : v);
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, 0x65);
    std::cout << "[INFO] Image " << (int)width << "x" << (int)height
              << "  png=" << png.size() << "B  frame=" << frame.size() << "B\n";
    write_raw_data(frame);
}

// ---------------------------------------------------------------------------
// save_image_file (save to slot, GIF aware)
// ---------------------------------------------------------------------------
void LEDController::save_image_file(const std::string& path,
                                     uint8_t slot, uint8_t width, uint8_t height)
{
    if (!connected_) return;
    if (slot < 1 || slot > 100) {
        std::cerr << "[!] Invalid slot number (1-100)\n";
        return;
    }

    std::string ext;
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
    }

    if (ext == ".gif") {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "[!] Cannot open GIF file: " << path << "\n";
            return;
        }
        std::vector<uint8_t> gif_data((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        auto frame = build_gif_frame(gif_data, slot);
        std::cout << "[INFO] Saving animated GIF (" << gif_data.size()
                  << " bytes) to slot " << (int)slot << "\n";
        write_raw_data(frame);
        std::cout << "[OK] GIF saved to device flash slot " << (int)slot << "\n";
        return;
    }

    std::cout << "[INFO] Loading image for save: " << path << "\n";
    auto rgb = load_image_for_panel(path, (int)width, (int)height);
    if (rgb.empty()) return;

    for (auto& ch : rgb) {
        int v = static_cast<int>(ch * brightness_);
        ch = static_cast<uint8_t>(v > 255 ? 255 : v);
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, slot);
    std::cout << "[INFO] Saving image " << (int)width << "x" << (int)height
              << " to slot " << (int)slot << "  png=" << png.size()
              << "B  frame=" << frame.size() << "B\n";
    write_raw_data(frame);
    std::cout << "[OK] Image saved to device flash slot " << (int)slot << "\n";
}

// ---------------------------------------------------------------------------
// save_text
// ---------------------------------------------------------------------------
void LEDController::save_text(const std::string& text,
                               uint8_t slot,
                               Color fg, Color bg,
                               const TextOptions& opts)
{
    if (!connected_) return;
    if (slot < 1 || slot > 100) {
        std::cerr << "[!] Invalid slot number (1-100)\n";
        return;
    }

    uint8_t effect_code = static_cast<uint8_t>(opts.effect);
    if (opts.effect == TextEffect::ScrollUp) {
        std::cerr << "[!] ScrollUp is not storable; using Fixed effect.\n";
        effect_code = static_cast<uint8_t>(TextEffect::Fixed);
    }

    uint8_t speed      = opts.speed < 1 ? 1 : opts.speed;
    uint8_t font_scale = opts.font_scale < 1 ? 1 : (opts.font_scale > 6 ? 6 : opts.font_scale);

    send_text_open_sequence(3);

    auto payload = build_text_payload(text, fg, bg, effect_code, speed, font_scale, slot);
    std::cout << "[INFO] Saving text \"" << text << "\" to slot " << (int)slot
              << " payload=" << payload.size() << "B\n";

    const int CHUNK_SIZE  = 509;
    const int INTERVAL_MS = 60;
    for (size_t offset = 0; offset < payload.size(); offset += CHUNK_SIZE) {
        size_t len = std::min<size_t>(CHUNK_SIZE, payload.size() - offset);
        std::vector<uint8_t> chunk(payload.begin() + offset,
                                   payload.begin() + offset + len);
        cmd_write(chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_MS));
    }
    std::cout << "[OK] Text saved to device flash slot " << (int)slot << "\n";
}

// ---------------------------------------------------------------------------
// send_text_file
// ---------------------------------------------------------------------------
void LEDController::send_text_file(const std::string& path,
                                    Color fg, Color bg, const TextOptions& opts)
{
    if (!connected_) return;

    std::ifstream f(path);
    if (!f) {
        std::cerr << "[!] Cannot open text file: " << path << "\n";
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end   = line.find_last_not_of(" \t\r\n");
        if (start != std::string::npos)
            lines.push_back(line.substr(start, end - start + 1));
    }

    if (lines.empty()) {
        std::cerr << "[!] Text file is empty or contains only whitespace\n";
        return;
    }

    std::string combined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) combined += "   ";
        combined += lines[i];
    }

    std::cout << "[INFO] Text from file (" << lines.size() << " line(s)): \""
              << combined << "\"\n";
    send_text(combined, fg, bg, opts);
}

// ---------------------------------------------------------------------------
// build_pattern_rgb
// ---------------------------------------------------------------------------
std::vector<uint8_t> LEDController::build_pattern_rgb(
    PatternType type, Color fg, Color bg, uint8_t width, uint8_t height)
{
    std::vector<uint8_t> rgb(width * height * 3);

    auto set_pixel = [&](int x, int y, Color c) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        int i = (y * width + x) * 3;
        rgb[i+0] = c.r; rgb[i+1] = c.g; rgb[i+2] = c.b;
    };

    for (int i = 0; i < width * height; ++i) {
        rgb[i*3+0] = bg.r; rgb[i*3+1] = bg.g; rgb[i*3+2] = bg.b;
    }

    switch (type) {
    case PatternType::CheckerTiny:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if ((x + y) % 2 == 0) set_pixel(x, y, fg);
        break;
    case PatternType::CheckerSmall:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (((x / 2) + (y / 2)) % 2 == 0) set_pixel(x, y, fg);
        break;
    case PatternType::CheckerMedium:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (((x / 3) + (y / 3)) % 2 == 0) set_pixel(x, y, fg);
        break;
    case PatternType::CheckerLarge:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (((x / 4) + (y / 4)) % 2 == 0) set_pixel(x, y, fg);
        break;
    case PatternType::HorizontalLines:
        for (int y = 0; y < height; ++y)
            if (y % 2 == 0)
                for (int x = 0; x < width; ++x) set_pixel(x, y, fg);
        break;
    case PatternType::VerticalLines:
        for (int x = 0; x < width; ++x)
            if (x % 2 == 0)
                for (int y = 0; y < height; ++y) set_pixel(x, y, fg);
        break;
    case PatternType::DiagonalLines:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if ((x + y) % 4 < 2) set_pixel(x, y, fg);
        break;
    case PatternType::DiagonalBack:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if ((x - y + height) % 4 < 2) set_pixel(x, y, fg);
        break;
    case PatternType::Zigzag:
        for (int y = 0; y < height; ++y) {
            int shift = (y / 2) % 4;
            for (int x = 0; x < width; ++x)
                if ((x + shift) % 4 < 2) set_pixel(x, y, fg);
        }
        break;
    case PatternType::Grid:
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (x % 4 == 0 || y % 4 == 0) set_pixel(x, y, fg);
        break;
    case PatternType::BorderSquare:
        for (int x = 0; x < width; ++x) {
            set_pixel(x, 0,          fg);
            set_pixel(x, height - 1, fg);
        }
        for (int y = 1; y < height - 1; ++y) {
            set_pixel(0,         y, fg);
            set_pixel(width - 1, y, fg);
        }
        break;
    case PatternType::RingSquares: {
        int rings = std::min(width, height) / 4;
        for (int r = 0; r <= rings; ++r) {
            int margin = r * 2;
            int x0 = margin, y0 = margin;
            int x1 = width  - 1 - margin;
            int y1 = height - 1 - margin;
            if (x0 > x1 || y0 > y1) break;
            for (int x = x0; x <= x1; ++x) {
                set_pixel(x, y0, fg); set_pixel(x, y1, fg);
            }
            for (int y = y0 + 1; y < y1; ++y) {
                set_pixel(x0, y, fg); set_pixel(x1, y, fg);
            }
        }
        break;
    }
    case PatternType::Crosshair: {
        int cx = width  / 2;
        int cy = height / 2;
        for (int x = 0; x < width;  ++x) set_pixel(x,  cy, fg);
        for (int y = 0; y < height; ++y) set_pixel(cx, y,  fg);
        set_pixel(cx, cy, bg);
        break;
    }
    case PatternType::GradientLeft:
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float t = (float)x / (float)(width - 1);
                Color c;
                c.r = (uint8_t)(bg.r + t * (fg.r - bg.r));
                c.g = (uint8_t)(bg.g + t * (fg.g - bg.g));
                c.b = (uint8_t)(bg.b + t * (fg.b - bg.b));
                set_pixel(x, y, c);
            }
        }
        break;
    case PatternType::GradientRight:
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float t = 1.0f - (float)x / (float)(width - 1);
                Color c;
                c.r = (uint8_t)(bg.r + t * (fg.r - bg.r));
                c.g = (uint8_t)(bg.g + t * (fg.g - bg.g));
                c.b = (uint8_t)(bg.b + t * (fg.b - bg.b));
                set_pixel(x, y, c);
            }
        }
        break;
    case PatternType::GradientDown:
        for (int y = 0; y < height; ++y) {
            float t = (float)y / (float)(height - 1);
            Color c;
            c.r = (uint8_t)(bg.r + t * (fg.r - bg.r));
            c.g = (uint8_t)(bg.g + t * (fg.g - bg.g));
            c.b = (uint8_t)(bg.b + t * (fg.b - bg.b));
            for (int x = 0; x < width; ++x) set_pixel(x, y, c);
        }
        break;
    case PatternType::GradientUp:
        for (int y = 0; y < height; ++y) {
            float t = 1.0f - (float)y / (float)(height - 1);
            Color c;
            c.r = (uint8_t)(bg.r + t * (fg.r - bg.r));
            c.g = (uint8_t)(bg.g + t * (fg.g - bg.g));
            c.b = (uint8_t)(bg.b + t * (fg.b - bg.b));
            for (int x = 0; x < width; ++x) set_pixel(x, y, c);
        }
        break;
    case PatternType::Dots:
        for (int y = 0; y < height; y += 4)
            for (int x = 0; x < width; x += 4)
                set_pixel(x, y, fg);
        break;
    }

    return rgb;
}

// ---------------------------------------------------------------------------
// send_pattern
// ---------------------------------------------------------------------------
void LEDController::send_pattern(PatternType type,
                                  Color fg, Color bg,
                                  uint8_t width, uint8_t height)
{
    if (!connected_) return;

    auto rgb = build_pattern_rgb(type, fg, bg, width, height);
    for (auto& ch : rgb) {
        int v = static_cast<int>(ch * brightness_);
        ch = static_cast<uint8_t>(v > 255 ? 255 : v);
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, 0x65);
    std::cout << "[INFO] Pattern sent  png=" << png.size()
              << "B  frame=" << frame.size() << "B\n";
    write_raw_data(frame);
}

// ---------------------------------------------------------------------------
// save_pattern
// ---------------------------------------------------------------------------
void LEDController::save_pattern(PatternType type,
                                  uint8_t slot,
                                  Color fg, Color bg,
                                  uint8_t width, uint8_t height)
{
    if (!connected_) return;
    if (slot < 1 || slot > 100) {
        std::cerr << "[!] Invalid slot number (1-100)\n";
        return;
    }

    auto rgb = build_pattern_rgb(type, fg, bg, width, height);
    for (auto& ch : rgb) {
        int v = static_cast<int>(ch * brightness_);
        ch = static_cast<uint8_t>(v > 255 ? 255 : v);
    }

    auto png   = build_png_from_rgb(rgb.data(), width, height);
    auto frame = build_image_frame(png, slot);
    std::cout << "[INFO] Saving pattern to slot " << (int)slot
              << "  png=" << png.size() << "B  frame=" << frame.size() << "B\n";
    write_raw_data(frame);
    std::cout << "[OK] Pattern saved to device flash slot " << (int)slot << "\n";
}

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------
void LEDController::delete_slots(const std::vector<uint8_t>& slots)
{
    if (!connected_) return;
    if (slots.empty()) {
        std::cerr << "[!] No slots to delete\n";
        return;
    }
    for (auto s : slots) {
        if (s < 1 || s > 100) {
            std::cerr << "[!] Invalid slot number " << (int)s << " (1-100)\n";
            return;
        }
    }

    uint16_t count = (uint16_t)slots.size();
    uint16_t cmd_len = 4 + 2 + count;
    std::vector<uint8_t> cmd;
    cmd.push_back( cmd_len        & 0xFF);
    cmd.push_back((cmd_len >>  8) & 0xFF);
    cmd.push_back(0x02);
    cmd.push_back(0x01);
    cmd.push_back( count        & 0xFF);
    cmd.push_back((count >>  8) & 0xFF);
    cmd.insert(cmd.end(), slots.begin(), slots.end());

    std::cout << "[INFO] Deleting " << (int)count << " slot(s)...\n";
    cmd_write(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "[OK] Slots deleted\n";
}

void LEDController::delete_all_slots()
{
    if (!connected_) return;
    std::vector<uint8_t> cmd = {0x04, 0x00, 0x03, 0x80};
    std::cout << "[INFO] Clearing all slots...\n";
    cmd_write(cmd);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "[OK] All slots cleared\n";
}

// ---------------------------------------------------------------------------
// Brightness
// ---------------------------------------------------------------------------
void LEDController::set_brightness(int percent)
{
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    brightness_ = percent / 100.0f;
    std::cout << "[INFO] Brightness set to " << percent << "%\n";
    if (has_last_frame_ && connected_)
        send_full_frame(last_r_, last_g_, last_b_, last_width_, last_height_);
}