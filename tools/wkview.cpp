#include "../src/common.h"
#include "../src/image_io.h"
#include <MiniFB.h>
#include <wk/wk.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct DisplayBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels;
};

using Glyph = std::array<uint8_t, 7>;

wk::Result<std::vector<uint8_t>> read_file_bytes(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to open file"});
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to determine file size"});
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to read file"});
    }
    return bytes;
}

uint16_t read_u16_le(const uint8_t* ptr) {
    return static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
}

uint8_t scale_to_u8(uint16_t sample, wk::BitDepth bit_depth) {
    const uint32_t max_value = (1u << static_cast<unsigned>(bit_depth)) - 1u;
    return static_cast<uint8_t>((static_cast<uint32_t>(sample) * 255u + max_value / 2u) / max_value);
}

uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(a) << 24)
         | (static_cast<uint32_t>(r) << 16)
         | (static_cast<uint32_t>(g) << 8)
         | static_cast<uint32_t>(b);
}

Glyph glyph_for(char ch) {
    switch (ch) {
        case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
        case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
        case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
        case 'J': return {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
        case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N': return {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
        case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
        case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1': return {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
        case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3': return {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
        case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5': return {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
        case '6': return {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
        case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
        case ':': return {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
        case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
        case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        default: return {0x1F, 0x01, 0x02, 0x04, 0x04, 0x00, 0x04};
    }
}

void fill_rect(DisplayBuffer& buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (x >= buffer.width || y >= buffer.height || width == 0 || height == 0) {
        return;
    }

    const uint32_t max_x = std::min(buffer.width, x + width);
    const uint32_t max_y = std::min(buffer.height, y + height);
    for (uint32_t row = y; row < max_y; ++row) {
        for (uint32_t col = x; col < max_x; ++col) {
            buffer.pixels[static_cast<size_t>(row) * buffer.width + col] = color;
        }
    }
}

void draw_glyph(DisplayBuffer& buffer, uint32_t x, uint32_t y, char ch, uint32_t color) {
    const Glyph glyph = glyph_for(ch);
    for (uint32_t row = 0; row < glyph.size(); ++row) {
        for (uint32_t col = 0; col < 5; ++col) {
            const uint8_t mask = static_cast<uint8_t>(1u << (4u - col));
            if ((glyph[row] & mask) == 0) {
                continue;
            }
            const uint32_t draw_x = x + col;
            const uint32_t draw_y = y + row;
            if (draw_x < buffer.width && draw_y < buffer.height) {
                buffer.pixels[static_cast<size_t>(draw_y) * buffer.width + draw_x] = color;
            }
        }
    }
}

std::string normalize_label(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char ch : text) {
        if (std::isalnum(ch) != 0) {
            result.push_back(static_cast<char>(std::toupper(ch)));
        } else if (ch == '.' || ch == ':' || ch == '-' || ch == '_' || ch == ' ') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back(' ');
        }
    }
    return result;
}

std::string fit_label_text(std::string text, uint32_t panel_width) {
    constexpr uint32_t glyph_width = 6;
    constexpr uint32_t text_padding = 10;
    if (panel_width <= text_padding * 2) {
        return {};
    }

    const size_t max_chars = (panel_width - text_padding * 2) / glyph_width;
    if (max_chars == 0) {
        return {};
    }
    if (text.size() <= max_chars) {
        return text;
    }
    if (max_chars <= 3) {
        return text.substr(0, max_chars);
    }
    return text.substr(0, max_chars - 3) + "...";
}

void draw_text(DisplayBuffer& buffer, uint32_t x, uint32_t y, std::string_view text, uint32_t color) {
    uint32_t cursor_x = x;
    for (char ch : text) {
        draw_glyph(buffer, cursor_x, y, ch, color);
        cursor_x += 6;
        if (cursor_x >= buffer.width) {
            break;
        }
    }
}

DisplayBuffer rasterize(const wk::Image& image) {
    DisplayBuffer display;
    display.width = image.width();
    display.height = image.height();
    display.pixels.resize(static_cast<size_t>(display.width) * display.height);

    const auto source = image.pixels();
    const bool has_alpha = image.has_alpha();

    if (image.bit_depth() == wk::BitDepth::Bits8) {
        const size_t bytes_per_pixel = has_alpha ? 4u : 3u;
        for (size_t i = 0; i < display.pixels.size(); ++i) {
            const uint8_t* pixel = source.data() + i * bytes_per_pixel;
            const uint8_t alpha = has_alpha ? pixel[3] : 255;
            display.pixels[i] = pack_rgba(pixel[0], pixel[1], pixel[2], alpha);
        }
        return display;
    }

    const size_t bytes_per_pixel = has_alpha ? 8u : 6u;
    for (size_t i = 0; i < display.pixels.size(); ++i) {
        const uint8_t* pixel = source.data() + i * bytes_per_pixel;
        const uint8_t r = scale_to_u8(read_u16_le(pixel + 0), image.bit_depth());
        const uint8_t g = scale_to_u8(read_u16_le(pixel + 2), image.bit_depth());
        const uint8_t b = scale_to_u8(read_u16_le(pixel + 4), image.bit_depth());
        const uint8_t a = has_alpha ? scale_to_u8(read_u16_le(pixel + 6), image.bit_depth()) : 255;
        display.pixels[i] = pack_rgba(r, g, b, a);
    }
    return display;
}

void blit_image(DisplayBuffer& canvas, const DisplayBuffer& image, uint32_t x, uint32_t y) {
    for (uint32_t row = 0; row < image.height; ++row) {
        std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(row) * image.width,
                    image.width,
                    canvas.pixels.begin() + static_cast<std::ptrdiff_t>(y + row) * canvas.width + x);
    }
}

DisplayBuffer compose_compare(const DisplayBuffer& decoded,
                              const DisplayBuffer* source,
                              std::string_view wk_path,
                              std::string_view source_path) {
    constexpr uint32_t outer_padding = 10;
    constexpr uint32_t panel_gap = 16;
    constexpr uint32_t label_height = 24;
    constexpr uint32_t label_text_y = 8;
    const uint32_t content_height = source == nullptr ? decoded.height : std::max(decoded.height, source->height);

    DisplayBuffer canvas;
    canvas.width = outer_padding * 2 + decoded.width;
    if (source != nullptr) {
        canvas.width += panel_gap + source->width;
    }
    canvas.height = outer_padding * 2 + label_height + content_height;
    canvas.pixels.assign(static_cast<size_t>(canvas.width) * canvas.height, pack_rgba(24, 24, 24, 255));

    const uint32_t decoded_x = outer_padding;
    const uint32_t source_x = outer_padding + decoded.width + panel_gap;
    const uint32_t image_y = outer_padding + label_height;
    const uint32_t decoded_y = image_y + (content_height - decoded.height) / 2u;

    fill_rect(canvas, decoded_x, outer_padding, decoded.width, label_height, pack_rgba(168, 112, 24, 255));
    draw_text(canvas,
              decoded_x + 10,
              outer_padding + label_text_y,
              fit_label_text(normalize_label("WK: " + std::filesystem::path(std::string(wk_path)).filename().string()), decoded.width),
              pack_rgba(255, 245, 220, 255));
    blit_image(canvas, decoded, decoded_x, decoded_y);

    if (source != nullptr) {
        const uint32_t source_y = image_y + (content_height - source->height) / 2u;
        fill_rect(canvas, decoded.width + outer_padding, outer_padding, panel_gap, canvas.height - outer_padding * 2, pack_rgba(56, 56, 56, 255));
        fill_rect(canvas, source_x, outer_padding, source->width, label_height, pack_rgba(26, 100, 128, 255));
        draw_text(canvas,
                  source_x + 10,
                  outer_padding + label_text_y,
                  fit_label_text(normalize_label("SOURCE: " + std::filesystem::path(std::string(source_path)).filename().string()), source->width),
                  pack_rgba(232, 248, 255, 255));
        blit_image(canvas, *source, source_x, source_y);
    }

    return canvas;
}

std::string make_title(std::string_view wk_path, const wk::Image& decoded, std::string_view source_path) {
    std::filesystem::path wk_file{std::string(wk_path)};
    std::string title = "WK Viewer - ";
    title += wk_file.filename().string();
    title += " - ";
    title += std::to_string(decoded.width());
    title += 'x';
    title += std::to_string(decoded.height());
    title += " - ";
    title += std::to_string(static_cast<unsigned>(decoded.bit_depth()));
    title += "-bit";
    if (!source_path.empty()) {
        std::filesystem::path src_file{std::string(source_path)};
        title += " - compare ";
        title += src_file.filename().string();
    }
    return title;
}

void print_usage() {
    std::cerr
        << "Usage: wkview <file.wk> [source.{jpg,jpeg,png,ppm}]\n"
        << "       wkview --help\n";
}

}

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help") {
        print_usage();
        return argc < 2 ? 1 : 0;
    }

    const std::string wk_path = argv[1];
    const std::string source_path = argc >= 3 ? argv[2] : std::string();

    auto wk_bytes = read_file_bytes(wk_path);
    if (!wk_bytes) {
        std::cerr << "Error: " << wk_bytes.error().message << '\n';
        return 1;
    }

    auto decoded = wk::decode(*wk_bytes);
    if (!decoded) {
        std::cerr << "Error: decode failed: " << decoded.error().message << '\n';
        return 1;
    }

    DisplayBuffer source_display;
    DisplayBuffer* source_ptr = nullptr;
    if (!source_path.empty()) {
        auto source_image = wk::io::load_image_file(source_path);
        if (!source_image) {
            std::cerr << "Error: failed to load source image: " << source_image.error().message << '\n';
            return 1;
        }
        source_display = rasterize(*source_image);
        source_ptr = &source_display;
    }

    const DisplayBuffer decoded_display = rasterize(*decoded);
    const DisplayBuffer canvas = compose_compare(decoded_display, source_ptr, wk_path, source_path);
    const unsigned initial_width = std::min(canvas.width, 1600u);
    const unsigned initial_height = std::min(canvas.height, 900u);

    mfb_window* window = mfb_open_ex(make_title(wk_path, *decoded, source_path).c_str(),
                                     initial_width,
                                     initial_height,
                                     MFB_WF_RESIZABLE);
    if (window == nullptr) {
        std::cerr << "Error: failed to create viewer window\n";
        return 1;
    }

    mfb_set_viewport_best_fit(window, canvas.width, canvas.height);
    unsigned last_window_width = 0;
    unsigned last_window_height = 0;

    while (true) {
        const mfb_update_state state = mfb_update_ex(window,
                                                     const_cast<uint32_t*>(canvas.pixels.data()),
                                                     canvas.width,
                                                     canvas.height);
        if (state != MFB_STATE_OK) {
            break;
        }

        const uint8_t* keys = mfb_get_key_buffer(window);
        if (keys != nullptr && keys[MFB_KB_KEY_ESCAPE]) {
            break;
        }

        const unsigned window_width = mfb_get_window_width(window);
        const unsigned window_height = mfb_get_window_height(window);
        if (window_width != last_window_width || window_height != last_window_height) {
            mfb_set_viewport_best_fit(window, canvas.width, canvas.height);
            last_window_width = window_width;
            last_window_height = window_height;
        }

        if (!mfb_wait_sync(window)) {
            break;
        }
    }

    mfb_close(window);
    return 0;
}
