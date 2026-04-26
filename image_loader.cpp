#include "image_loader.h"

#include <iostream>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// stb_image — drop stb_image.h into the project root (or any include path).
// Download: https://github.com/nothings/stb/blob/master/stb_image.h
// ---------------------------------------------------------------------------
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif

#ifdef __has_include
#  if __has_include("stb_image.h")
#    include "stb_image.h"
#  else
#    error "image_loader.cpp requires stb_image.h. \
Download it from https://github.com/nothings/stb/blob/master/stb_image.h \
and place it next to image_loader.cpp."
#  endif
#else
#  include "stb_image.h"
#endif

// ---------------------------------------------------------------------------
// load_image_for_panel
// ---------------------------------------------------------------------------
std::vector<uint8_t> load_image_for_panel(const std::string& path,
                                           int target_w, int target_h)
{
    if (target_w <= 0 || target_h <= 0) {
        std::cerr << "[!] load_image_for_panel: invalid target dimensions "
                  << target_w << "x" << target_h << "\n";
        return {};
    }

    int src_w = 0, src_h = 0, channels = 0;

    // Request 3 output channels (RGB) regardless of source format.
    // stb_image handles GIF by decoding the first frame automatically.
    uint8_t* pixels = stbi_load(path.c_str(), &src_w, &src_h, &channels, 3);
    if (!pixels) {
        std::cerr << "[!] load_image_for_panel: cannot load '" << path
                  << "': " << stbi_failure_reason() << "\n";
        return {};
    }

    // -----------------------------------------------------------------------
    // Compute the largest rectangle that fits inside target_w × target_h
    // while preserving the source aspect ratio (integer or float).
    // -----------------------------------------------------------------------
    int scaled_w, scaled_h;
    {
        // Try fitting by width first.
        scaled_w = target_w;
        scaled_h = (int)((float)src_h * target_w / src_w + 0.5f);
        if (scaled_h > target_h) {
            // Doesn't fit height-wise — fit by height instead.
            scaled_h = target_h;
            scaled_w = (int)((float)src_w * target_h / src_h + 0.5f);
        }
    }
    scaled_w = std::max(1, std::min(scaled_w, target_w));
    scaled_h = std::max(1, std::min(scaled_h, target_h));

    // -----------------------------------------------------------------------
    // Scale source into scaled_w × scaled_h using bilinear interpolation.
    // -----------------------------------------------------------------------
    std::vector<uint8_t> scaled(scaled_w * scaled_h * 3);
    for (int dy = 0; dy < scaled_h; ++dy) {
        float fy = (float)dy * (src_h - 1) / std::max(1, scaled_h - 1);
        int   y0 = (int)fy;
        int   y1 = std::min(y0 + 1, src_h - 1);
        float vy = fy - y0;

        for (int dx = 0; dx < scaled_w; ++dx) {
            float fx = (float)dx * (src_w - 1) / std::max(1, scaled_w - 1);
            int   x0 = (int)fx;
            int   x1 = std::min(x0 + 1, src_w - 1);
            float vx = fx - x0;

            for (int c = 0; c < 3; ++c) {
                float p00 = pixels[(y0 * src_w + x0) * 3 + c];
                float p10 = pixels[(y0 * src_w + x1) * 3 + c];
                float p01 = pixels[(y1 * src_w + x0) * 3 + c];
                float p11 = pixels[(y1 * src_w + x1) * 3 + c];
                float val = p00 * (1 - vx) * (1 - vy)
                          + p10 * vx       * (1 - vy)
                          + p01 * (1 - vx) * vy
                          + p11 * vx       * vy;
                scaled[(dy * scaled_w + dx) * 3 + c] =
                    (uint8_t)std::max(0.f, std::min(255.f, val + 0.5f));
            }
        }
    }
    stbi_image_free(pixels);

    // -----------------------------------------------------------------------
    // Blit scaled image centred into a black target_w × target_h canvas
    // (letterbox bars top/bottom, or pillarbox bars left/right).
    // -----------------------------------------------------------------------
    std::vector<uint8_t> canvas(target_w * target_h * 3, 0x00); // black fill

    int offset_x = (target_w - scaled_w) / 2;
    int offset_y = (target_h - scaled_h) / 2;

    for (int row = 0; row < scaled_h; ++row) {
        int dst_row = row + offset_y;
        if (dst_row < 0 || dst_row >= target_h) continue;
        int dst_col_start = offset_x;
        // Clamp in case rounding pushed us 1px over.
        int copy_w = std::min(scaled_w, target_w - dst_col_start);
        if (copy_w <= 0) continue;
        std::memcpy(
            canvas.data() + (dst_row * target_w + dst_col_start) * 3,
            scaled.data() + row * scaled_w * 3,
            copy_w * 3
        );
    }

    return canvas;
}