#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <string>
#include <vector>
#include <cstdint>

// Load any image format supported by stb_image (PNG, JPG, BMP, GIF first frame)
// and produce a packed RGB buffer of exactly width*height*3 bytes.
//
// Non-square sources are letterboxed / pillarboxed with black bars so the
// content is centred and the aspect ratio is preserved.  No intermediate file
// is written to disk.
//
// Returns an empty vector on failure (error printed to stderr).
std::vector<uint8_t> load_image_for_panel(const std::string& path,
                                           int width, int height);

#endif