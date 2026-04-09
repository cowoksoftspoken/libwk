#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace wk {


struct Error;
enum class ErrorCode : uint32_t;

template<typename T>
using Result = std::expected<T, Error>;



enum class BitDepth : uint8_t { Bits8 = 8, Bits10 = 10, Bits12 = 12 };

enum class Subsampling : uint8_t { YUV444 = 0, YUV420 = 1 };

struct CICP {
    uint8_t primaries  = 1;
    uint8_t transfer   = 1;
    uint8_t matrix     = 1;
    bool    full_range = true;
};

struct ImageInfo {
    uint32_t     width         = 0;
    uint32_t     height        = 0;
    BitDepth     bit_depth     = BitDepth::Bits8;
    CICP         cicp          = {};
    bool         has_alpha     = false;
    bool         is_lossless   = false;
    bool         is_animated   = false;
    bool         is_hdr        = false;
    bool         has_wkmeta    = false;
    uint32_t     tile_size     = 512;
    uint16_t     max_cll       = 0;
    uint16_t     max_fall      = 0;
    uint32_t     frame_count   = 1;
};

class Image {
public:
    Image() = default;
    Image(uint32_t w, uint32_t h, BitDepth bd = BitDepth::Bits8, bool alpha = true);

    [[nodiscard]] uint32_t width() const { return info_.width; }
    [[nodiscard]] uint32_t height() const { return info_.height; }
    [[nodiscard]] BitDepth bit_depth() const { return info_.bit_depth; }
    [[nodiscard]] bool has_alpha() const { return info_.has_alpha; }
    [[nodiscard]] const ImageInfo& info() const { return info_; }
    [[nodiscard]] ImageInfo& info() { return info_; }


    [[nodiscard]] std::span<uint8_t> pixels() { return pixels_; }
    [[nodiscard]] std::span<const uint8_t> pixels() const { return pixels_; }


    [[nodiscard]] uint8_t* row(uint32_t y);
    [[nodiscard]] const uint8_t* row(uint32_t y) const;


    [[nodiscard]] uint32_t bytes_per_pixel() const;


    [[nodiscard]] size_t stride() const;

private:
    ImageInfo            info_;
    std::vector<uint8_t> pixels_;
};



struct EncoderConfig {
    float        quality             = 75.0f;
    bool         lossless            = false;
    BitDepth     bit_depth           = BitDepth::Bits8;
    Subsampling  subsampling         = Subsampling::YUV420;
    CICP         cicp                = {};
    uint8_t      tile_size_log2      = 9;
    uint32_t     threads             = 0;
    float        target_ssimulacra2  = 0.0f;
};


[[nodiscard]] Result<std::vector<uint8_t>> encode(const Image& image, const EncoderConfig& config = {});



struct DecoderConfig {
    uint32_t     threads       = 0;
    bool         info_only     = false;
    bool         dc_only       = false;
};


[[nodiscard]] Result<ImageInfo> get_info(std::span<const uint8_t> data);


[[nodiscard]] Result<Image> decode(std::span<const uint8_t> data, const DecoderConfig& config = {});



constexpr std::string_view version() { return "0.1.0"; }

}
