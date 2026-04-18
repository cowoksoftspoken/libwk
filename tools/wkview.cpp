#include "../src/common.h"
#include "../src/image_io.h"
#include <MiniFB.h>
#include <wk/wk.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct DisplayBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels;
};

struct ViewerPanel {
    DisplayBuffer image;
    std::string label;
    std::vector<std::string> info_lines;
    uint32_t label_bg = 0;
    uint32_t label_text = 0;
    uint32_t info_bg = 0;
    uint32_t info_text = 0;
};

using Glyph = std::array<uint8_t, 7>;

std::string lowercase_ascii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::vector<std::filesystem::path> suggest_similar_files(std::string_view path) {
    std::filesystem::path requested{std::string(path)};
    std::filesystem::path dir = requested.parent_path().empty() ? std::filesystem::path(".") : requested.parent_path();
    const std::string requested_stem = lowercase_ascii(requested.stem().string());
    const std::string requested_ext = lowercase_ascii(requested.extension().string());

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        return {};
    }

    struct Candidate {
        int score = 0;
        std::filesystem::path path;
    };

    std::vector<Candidate> candidates;
    for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            continue;
        }

        const std::filesystem::path candidate_path = it->path();
        const std::string candidate_ext = lowercase_ascii(candidate_path.extension().string());
        const std::string candidate_stem = lowercase_ascii(candidate_path.stem().string());
        if (!requested_ext.empty() && candidate_ext != requested_ext) {
            continue;
        }

        int score = 0;
        if (candidate_stem == requested_stem) {
            score += 100;
        }
        if (!requested_stem.empty() && candidate_stem.rfind(requested_stem, 0) == 0) {
            score += 40;
        }
        if (!requested_stem.empty() && candidate_stem.find(requested_stem) != std::string::npos) {
            score += 20;
        }
        if (!requested_stem.empty() && requested_stem.find(candidate_stem) != std::string::npos) {
            score += 10;
        }
        if (candidate_ext == requested_ext) {
            score += 5;
        }

        if (score > 0) {
            candidates.push_back({score, candidate_path});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.path.filename().string().size() < rhs.path.filename().string().size();
    });

    std::vector<std::filesystem::path> results;
    for (const Candidate& candidate : candidates) {
        if (results.size() == 3) {
            break;
        }
        results.push_back(candidate.path);
    }
    return results;
}

std::string make_missing_file_message(std::string_view path) {
    std::string message = "failed to open file: ";
    message += std::string(path);

    const auto suggestions = suggest_similar_files(path);
    if (!suggestions.empty()) {
        message += ". did you mean ";
        for (size_t i = 0; i < suggestions.size(); ++i) {
            if (i > 0) {
                message += i + 1 == suggestions.size() ? " or " : ", ";
            }
            message += suggestions[i].generic_string();
        }
        message += " ?";
    }

    return message;
}

wk::Result<std::vector<uint8_t>> read_file_bytes(std::string_view path) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::string(path)), ec) || ec) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, make_missing_file_message(path)});
    }

    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to open file: " + std::string(path)});
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to determine file size: " + std::string(path)});
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to read file: " + std::string(path)});
    }
    return bytes;
}

wk::Result<wk::Image> load_image_file(std::string_view path) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::string(path)), ec) || ec) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, make_missing_file_message(path)});
    }
    return wk::io::load_image_file(path);
}

uintmax_t file_size_or_zero(std::string_view path) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(std::filesystem::path(std::string(path)), ec);
    return ec ? 0u : size;
}

uint16_t read_u16_le(const uint8_t* ptr) {
    return static_cast<uint16_t>(ptr[0]) | (static_cast<uint16_t>(ptr[1]) << 8);
}

uint8_t scale_to_u8(uint16_t sample, wk::BitDepth bit_depth) {
    const uint32_t max_value = (1u << static_cast<unsigned>(bit_depth)) - 1u;
    return static_cast<uint8_t>((static_cast<uint32_t>(sample) * 255u + max_value / 2u) / max_value);
}

uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return MFB_ARGB(a, r, g, b);
}

uint8_t channel_b(uint32_t pixel) {
    return static_cast<uint8_t>(pixel & 0xffu);
}

uint8_t channel_g(uint32_t pixel) {
    return static_cast<uint8_t>((pixel >> 8) & 0xffu);
}

uint8_t channel_r(uint32_t pixel) {
    return static_cast<uint8_t>((pixel >> 16) & 0xffu);
}

uint8_t channel_a(uint32_t pixel) {
    return static_cast<uint8_t>((pixel >> 24) & 0xffu);
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

std::string bool_text(bool value) {
    return value ? "YES" : "NO";
}

std::string format_size_human(uintmax_t bytes) {
    static constexpr std::array<std::string_view, 4> units = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < units.size()) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream stream;
    if (unit_index == 0) {
        stream << bytes << ' ' << units[unit_index];
    } else {
        stream << std::fixed << std::setprecision(value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2));
        stream << value << ' ' << units[unit_index];
    }
    return stream.str();
}

std::string file_extension_label(std::string_view path) {
    std::string ext = std::filesystem::path(std::string(path)).extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    if (ext.empty()) {
        return "FILE";
    }
    return normalize_label(ext);
}

std::vector<std::string> make_wk_info_lines(std::string_view path, uintmax_t bytes, const wk::ImageInfo& info) {
    return {
        "FILE: " + std::filesystem::path(std::string(path)).filename().string(),
        "SIZE: " + std::to_string(bytes) + " B " + format_size_human(bytes),
        "DIM: " + std::to_string(info.width) + "X" + std::to_string(info.height),
        "DEPTH: " + std::to_string(static_cast<unsigned>(info.bit_depth)) + " BIT",
        "MODE: " + std::string(info.is_lossless ? "LOSSLESS" : "LOSSY"),
        "ALPHA: " + bool_text(info.has_alpha),
        "WKMETA: " + bool_text(info.has_wkmeta),
        "HDR: " + bool_text(info.is_hdr),
        "TILE: " + std::to_string(info.tile_size) + " PX",
        "FRAMES: " + std::to_string(info.frame_count),
        "CICP: P" + std::to_string(info.cicp.primaries) + " T" + std::to_string(info.cicp.transfer) +
            " M" + std::to_string(info.cicp.matrix) + " " + (info.cicp.full_range ? std::string("FULL") : std::string("LIMITED"))
    };
}

std::vector<std::string> make_source_info_lines(std::string_view path, uintmax_t bytes, const wk::Image& image) {
    return {
        "FILE: " + std::filesystem::path(std::string(path)).filename().string(),
        "SIZE: " + std::to_string(bytes) + " B " + format_size_human(bytes),
        "FORMAT: " + file_extension_label(path),
        "DIM: " + std::to_string(image.width()) + "X" + std::to_string(image.height()),
        "DEPTH: " + std::to_string(static_cast<unsigned>(image.bit_depth())) + " BIT",
        "ALPHA: " + bool_text(image.has_alpha())
    };
}

uint32_t info_block_height(const ViewerPanel& panel) {
    constexpr uint32_t line_height = 10;
    constexpr uint32_t padding_y = 8;
    return padding_y * 2 + static_cast<uint32_t>(panel.info_lines.size()) * line_height;
}

void draw_info_block(DisplayBuffer& canvas, const ViewerPanel& panel, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    fill_rect(canvas, x, y, width, height, panel.info_bg);
    constexpr uint32_t line_height = 10;
    constexpr uint32_t padding_x = 10;
    constexpr uint32_t padding_y = 8;
    uint32_t cursor_y = y + padding_y;
    for (const std::string& line : panel.info_lines) {
        draw_text(canvas,
                  x + padding_x,
                  cursor_y,
                  fit_label_text(normalize_label(line), width),
                  panel.info_text);
        cursor_y += line_height;
    }
}

struct FitRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

FitRect best_fit_rect(uint32_t src_width, uint32_t src_height, uint32_t dst_width, uint32_t dst_height) {
    if (src_width == 0 || src_height == 0 || dst_width == 0 || dst_height == 0) {
        return {};
    }

    const double scale = std::min(static_cast<double>(dst_width) / static_cast<double>(src_width),
                                  static_cast<double>(dst_height) / static_cast<double>(src_height));
    const uint32_t fit_width = std::max(1u, static_cast<uint32_t>(std::lround(src_width * scale)));
    const uint32_t fit_height = std::max(1u, static_cast<uint32_t>(std::lround(src_height * scale)));

    FitRect rect;
    rect.width = std::min(fit_width, dst_width);
    rect.height = std::min(fit_height, dst_height);
    rect.x = (dst_width - rect.width) / 2u;
    rect.y = (dst_height - rect.height) / 2u;
    return rect;
}

uint32_t sample_bilinear(const DisplayBuffer& source, double src_x, double src_y) {
    const double clamped_x = std::clamp(src_x, 0.0, static_cast<double>(source.width - 1));
    const double clamped_y = std::clamp(src_y, 0.0, static_cast<double>(source.height - 1));
    const uint32_t x0 = static_cast<uint32_t>(clamped_x);
    const uint32_t y0 = static_cast<uint32_t>(clamped_y);
    const uint32_t x1 = std::min(x0 + 1, source.width - 1);
    const uint32_t y1 = std::min(y0 + 1, source.height - 1);
    const double tx = clamped_x - static_cast<double>(x0);
    const double ty = clamped_y - static_cast<double>(y0);

    const uint32_t p00 = source.pixels[static_cast<size_t>(y0) * source.width + x0];
    const uint32_t p10 = source.pixels[static_cast<size_t>(y0) * source.width + x1];
    const uint32_t p01 = source.pixels[static_cast<size_t>(y1) * source.width + x0];
    const uint32_t p11 = source.pixels[static_cast<size_t>(y1) * source.width + x1];

    auto interpolate_channel = [&](uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11) -> uint8_t {
        const double top = static_cast<double>(c00) + (static_cast<double>(c10) - static_cast<double>(c00)) * tx;
        const double bottom = static_cast<double>(c01) + (static_cast<double>(c11) - static_cast<double>(c01)) * tx;
        return static_cast<uint8_t>(std::clamp(std::lround(top + (bottom - top) * ty), 0l, 255l));
    };

    return pack_rgba(
        interpolate_channel(channel_r(p00), channel_r(p10), channel_r(p01), channel_r(p11)),
        interpolate_channel(channel_g(p00), channel_g(p10), channel_g(p01), channel_g(p11)),
        interpolate_channel(channel_b(p00), channel_b(p10), channel_b(p01), channel_b(p11)),
        interpolate_channel(channel_a(p00), channel_a(p10), channel_a(p01), channel_a(p11)));
}

void draw_scaled_image(DisplayBuffer& canvas,
                       const DisplayBuffer& image,
                       uint32_t x,
                       uint32_t y,
                       uint32_t width,
                       uint32_t height,
                       uint32_t background) {
    fill_rect(canvas, x, y, width, height, background);
    if (image.width == 0 || image.height == 0 || width == 0 || height == 0) {
        return;
    }

    const FitRect fit = best_fit_rect(image.width, image.height, width, height);
    if (fit.width == 0 || fit.height == 0) {
        return;
    }

    if (fit.width == image.width && fit.height == image.height) {
        blit_image(canvas, image, x + fit.x, y + fit.y);
        return;
    }

    const bool shrinking = fit.width < image.width || fit.height < image.height;
    for (uint32_t dst_y = 0; dst_y < fit.height; ++dst_y) {
        for (uint32_t dst_x = 0; dst_x < fit.width; ++dst_x) {
            const double src_x = (static_cast<double>(dst_x) + 0.5) * static_cast<double>(image.width) / static_cast<double>(fit.width) - 0.5;
            const double src_y = (static_cast<double>(dst_y) + 0.5) * static_cast<double>(image.height) / static_cast<double>(fit.height) - 0.5;

            uint32_t pixel = 0;
            if (shrinking) {
                pixel = sample_bilinear(image, src_x, src_y);
            } else {
                const uint32_t nearest_x = std::min(static_cast<uint32_t>(std::lround(std::clamp(src_x, 0.0, static_cast<double>(image.width - 1)))), image.width - 1);
                const uint32_t nearest_y = std::min(static_cast<uint32_t>(std::lround(std::clamp(src_y, 0.0, static_cast<double>(image.height - 1)))), image.height - 1);
                pixel = image.pixels[static_cast<size_t>(nearest_y) * image.width + nearest_x];
            }

            const uint32_t out_x = x + fit.x + dst_x;
            const uint32_t out_y = y + fit.y + dst_y;
            if (out_x < canvas.width && out_y < canvas.height) {
                canvas.pixels[static_cast<size_t>(out_y) * canvas.width + out_x] = pixel;
            }
        }
    }
}

DisplayBuffer compose_window(const ViewerPanel& decoded_panel,
                             const ViewerPanel* source_panel,
                             uint32_t window_width,
                             uint32_t window_height) {
    constexpr uint32_t outer_padding = 10;
    constexpr uint32_t panel_gap = 16;
    constexpr uint32_t label_height = 24;
    constexpr uint32_t label_text_y = 8;
    constexpr uint32_t info_gap = 10;
    constexpr uint32_t min_image_height = 160;
    const uint32_t panel_count = source_panel == nullptr ? 1u : 2u;

    DisplayBuffer canvas;
    canvas.width = std::max(1u, window_width);
    canvas.height = std::max(1u, window_height);
    canvas.pixels.assign(static_cast<size_t>(canvas.width) * canvas.height, pack_rgba(24, 24, 24, 255));

    const uint32_t target_info_height = std::max(info_block_height(decoded_panel),
                                                 source_panel == nullptr ? 0u : info_block_height(*source_panel));
    const uint32_t base_reserved = outer_padding * 2 + label_height + info_gap;
    uint32_t info_height = 0;
    if (canvas.height > base_reserved + min_image_height) {
        info_height = std::min(target_info_height, canvas.height - base_reserved - min_image_height);
    }
    const uint32_t image_height = std::max(1u, canvas.height - base_reserved - info_height);

    const uint32_t available_width = canvas.width > outer_padding * 2 ? canvas.width - outer_padding * 2 : 1u;
    const uint32_t divider_width = panel_count == 2 && available_width > 2u
        ? std::min(panel_gap, available_width - 2u)
        : 0u;
    const uint32_t total_panel_width = available_width > divider_width ? available_width - divider_width : 1u;
    const uint32_t decoded_width = panel_count == 2 ? std::max(1u, total_panel_width / 2u) : std::max(1u, total_panel_width);
    const uint32_t source_width = panel_count == 2 ? total_panel_width - decoded_width : 0u;

    const uint32_t label_y = outer_padding;
    const uint32_t image_y = label_y + label_height;
    const uint32_t info_y = image_y + image_height + info_gap;
    const uint32_t image_bg = pack_rgba(10, 10, 10, 255);

    auto draw_panel = [&](const ViewerPanel& panel, uint32_t x, uint32_t width) {
        if (width == 0) {
            return;
        }
        fill_rect(canvas, x, label_y, width, label_height, panel.label_bg);
        draw_text(canvas,
                  x + 10,
                  label_y + label_text_y,
                  fit_label_text(normalize_label(panel.label), width),
                  panel.label_text);
        draw_scaled_image(canvas, panel.image, x, image_y, width, image_height, image_bg);
        if (info_height > 0) {
            draw_info_block(canvas, panel, x, info_y, width, info_height);
        }
    };

    draw_panel(decoded_panel, outer_padding, decoded_width);

    if (source_panel != nullptr) {
        const uint32_t divider_x = outer_padding + decoded_width;
        fill_rect(canvas,
                  divider_x,
                  outer_padding,
                  divider_width,
                  canvas.height - outer_padding * 2,
                  pack_rgba(56, 56, 56, 255));
        draw_panel(*source_panel, divider_x + divider_width, source_width);
    }

    return canvas;
}

std::string make_title(std::string_view wk_path, const wk::ImageInfo& info, std::string_view source_path) {
    std::filesystem::path wk_file{std::string(wk_path)};
    std::string title = "WK Viewer - ";
    title += wk_file.filename().string();
    title += " - ";
    title += std::to_string(info.width);
    title += 'x';
    title += std::to_string(info.height);
    title += " - ";
    title += info.is_lossless ? "LOSSLESS" : "LOSSY";
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

    auto info = wk::get_info(*wk_bytes);
    if (!info) {
        std::cerr << "Error: failed to read WK info: " << info.error().message << '\n';
        return 1;
    }

    auto decoded = wk::decode(*wk_bytes);
    if (!decoded) {
        std::cerr << "Error: decode failed: " << decoded.error().message << '\n';
        return 1;
    }

    ViewerPanel decoded_panel;
    decoded_panel.image = rasterize(*decoded);
    decoded_panel.label = "WK: " + std::filesystem::path(wk_path).filename().string();
    decoded_panel.info_lines = make_wk_info_lines(wk_path, file_size_or_zero(wk_path), *info);
    decoded_panel.label_bg = pack_rgba(168, 112, 24, 255);
    decoded_panel.label_text = pack_rgba(255, 245, 220, 255);
    decoded_panel.info_bg = pack_rgba(44, 34, 18, 255);
    decoded_panel.info_text = pack_rgba(255, 236, 196, 255);

    ViewerPanel source_panel;
    ViewerPanel* source_ptr = nullptr;
    if (!source_path.empty()) {
        auto source_image = load_image_file(source_path);
        if (!source_image) {
            std::cerr << "Error: failed to load source image: " << source_image.error().message << '\n';
            return 1;
        }
        source_panel.image = rasterize(*source_image);
        source_panel.label = "SOURCE: " + std::filesystem::path(source_path).filename().string();
        source_panel.info_lines = make_source_info_lines(source_path, file_size_or_zero(source_path), *source_image);
        source_panel.label_bg = pack_rgba(26, 100, 128, 255);
        source_panel.label_text = pack_rgba(232, 248, 255, 255);
        source_panel.info_bg = pack_rgba(18, 39, 54, 255);
        source_panel.info_text = pack_rgba(220, 244, 255, 255);
        source_ptr = &source_panel;
    }

    const uint32_t preferred_content_height = std::max(decoded_panel.image.height,
                                                       source_ptr == nullptr ? 0u : source_ptr->image.height);
    const uint32_t preferred_info_height = std::max(info_block_height(decoded_panel),
                                                    source_ptr == nullptr ? 0u : info_block_height(*source_ptr));
    const uint32_t preferred_width = source_ptr == nullptr
        ? decoded_panel.image.width + 20u
        : decoded_panel.image.width + source_ptr->image.width + 36u;
    const uint32_t preferred_height = preferred_content_height + preferred_info_height + 54u;
    const unsigned initial_width = static_cast<unsigned>(std::clamp<uint32_t>(preferred_width,
                                                                              source_ptr == nullptr ? 720u : 1200u,
                                                                              1800u));
    const unsigned initial_height = static_cast<unsigned>(std::clamp<uint32_t>(preferred_height, 520u, 1000u));

    mfb_window* window = mfb_open_ex(make_title(wk_path, *info, source_path).c_str(),
                                     initial_width,
                                     initial_height,
                                     MFB_WF_RESIZABLE);
    if (window == nullptr) {
        std::cerr << "Error: failed to create viewer window\n";
        return 1;
    }

    DisplayBuffer present = compose_window(decoded_panel, source_ptr, initial_width, initial_height);
    mfb_set_viewport(window, 0, 0, present.width, present.height);
    unsigned last_window_width = 0;
    unsigned last_window_height = 0;

    while (true) {
        const unsigned window_width = std::max(1u, mfb_get_window_width(window));
        const unsigned window_height = std::max(1u, mfb_get_window_height(window));
        if (window_width != last_window_width || window_height != last_window_height) {
            present = compose_window(decoded_panel, source_ptr, window_width, window_height);
            mfb_set_viewport(window, 0, 0, present.width, present.height);
            last_window_width = window_width;
            last_window_height = window_height;
        }

        const mfb_update_state state = mfb_update_ex(window,
                                                     const_cast<uint32_t*>(present.pixels.data()),
                                                     present.width,
                                                     present.height);
        if (state != MFB_STATE_OK) {
            break;
        }
        const uint8_t* keys = mfb_get_key_buffer(window);
        if (keys != nullptr && keys[MFB_KB_KEY_ESCAPE]) {
            break;
        }

        if (!mfb_wait_sync(window)) {
            break;
        }
    }

    mfb_close(window);
    return 0;
}