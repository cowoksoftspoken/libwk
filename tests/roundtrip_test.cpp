
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <wk/wk.hpp>
#include "../src/common.h"
#include "../src/coeff_sign_stream.h"
#include "../src/container.h"
#include "../src/coeff_span_stream.h"
#include "../src/coeff_table_stream.h"
#include "../src/image_io.h"
#include "../src/metrics.h"
#include "../src/mode_stream.h"
#include "../src/rans.h"
#include <cmath>
#include <filesystem>

using namespace wk;

namespace {

Image make_rgb_gradient(uint32_t width, uint32_t height) {
    Image image(width, height, BitDepth::Bits8, false);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 3 + 0] = static_cast<uint8_t>((x * 255u) / std::max(width - 1, 1u));
            row[x * 3 + 1] = static_cast<uint8_t>((y * 255u) / std::max(height - 1, 1u));
            row[x * 3 + 2] = static_cast<uint8_t>(((x + y) * 255u) / std::max(width + height - 2, 1u));
        }
    }
    return image;
}

Image make_dark_color_scene(uint32_t width, uint32_t height) {
    Image image(width, height, BitDepth::Bits8, false);
    auto clamp_byte = [](float value) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
    };
    auto glow = [](float dx, float dy, float sigma) -> float {
        return std::exp(-(dx * dx + dy * dy) / sigma);
    };

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = image.row(y);
        const float fy = static_cast<float>(y) / std::max(height - 1, 1u);
        for (uint32_t x = 0; x < width; ++x) {
            const float fx = static_cast<float>(x) / std::max(width - 1, 1u);
            const float amber = glow(fx - 0.32f, fy - 0.24f, 0.010f);
            const float cyan = glow(fx - 0.72f, fy - 0.36f, 0.014f);
            const float magenta = glow(fx - 0.50f, fy - 0.68f, 0.020f);
            const float horizon = std::exp(-std::abs(fy - 0.55f) * 18.0f);
            const float texture = 0.5f + 0.5f * std::sin(fx * 28.0f + fy * 8.0f);
            const float reflection = fy > 0.55f ? (fy - 0.55f) * 1.8f : 0.0f;
            const float neon_band = (fy > 0.48f && fy < 0.52f) ? 1.0f - std::min(1.0f, std::abs(fx - 0.52f) * 5.0f) : 0.0f;

            float r = 0.03f + 0.05f * (1.0f - fy) + 0.72f * amber + 0.24f * magenta + 0.10f * horizon + 0.03f * texture;
            float g = 0.04f + 0.06f * (1.0f - fy) + 0.40f * amber + 0.46f * cyan + 0.08f * horizon + 0.02f * texture;
            float b = 0.08f + 0.14f * (1.0f - fy) + 0.82f * cyan + 0.18f * magenta + 0.10f * reflection + 0.05f * texture;

            if (fy > 0.55f) {
                r += 0.25f * amber * reflection;
                g += 0.15f * cyan * reflection;
                b += 0.32f * (cyan + magenta) * reflection;
            }
            r += 0.18f * neon_band;
            g += 0.12f * neon_band;
            b += 0.28f * neon_band;

            row[x * 3 + 0] = clamp_byte(std::clamp(r, 0.0f, 1.0f));
            row[x * 3 + 1] = clamp_byte(std::clamp(g, 0.0f, 1.0f));
            row[x * 3 + 2] = clamp_byte(std::clamp(b, 0.0f, 1.0f));
        }
    }
    return image;
}

std::filesystem::path photos_root() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "photos";
}

std::filesystem::path find_photo_named(std::string_view name) {
    const std::filesystem::path root = photos_root();
    const std::array<std::filesystem::path, 3> preferred = {
        root / std::string(name),
        root / "people" / std::string(name),
        root / "scenery" / std::string(name)
    };
    for (const auto& candidate : preferred) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            continue;
        }
        if (it->path().filename() == name) {
            return it->path();
        }
    }
    return {};
}

std::filesystem::path sample_photo_path() {
    return find_photo_named("ember-7f3a.jpg");
}

constexpr size_t kTileHeaderBytes = 9u;
constexpr size_t kQuantTableBytes = 64u * sizeof(uint16_t) * 2u;
constexpr size_t kBlockDimensionBytes = 4u * sizeof(uint16_t);
constexpr size_t kLayoutTagBytes = sizeof(uint32_t);
constexpr size_t kLossySyntaxBaseOffset = kTileHeaderBytes + kQuantTableBytes + kBlockDimensionBytes;

struct LossyTileSyntaxInfo {
    uint32_t layout_tag = 0;
    uint8_t syntax_flags = 0;
    size_t stream_offset = 0;
    bool adaptive_spans = false;
    bool plane_extents = false;
    bool split_magnitude_signs = false;
    bool shared_chroma_tables = false;
};

Result<LossyTileSyntaxInfo> parse_lossy_tile_syntax(std::span<const uint8_t> payload) {
    if (payload.size() < kLossySyntaxBaseOffset + kLayoutTagBytes) {
        return std::unexpected(Error{ErrorCode::TruncatedInput, "lossy payload is missing layout tag"});
    }

    LossyTileSyntaxInfo info;
    info.layout_tag = read_le32(payload.data() + kLossySyntaxBaseOffset);
    info.stream_offset = kLossySyntaxBaseOffset + kLayoutTagBytes;
    if (info.layout_tag == kLossyTileLayoutTagV6) {
        if (payload.size() <= info.stream_offset) {
            return std::unexpected(Error{ErrorCode::TruncatedInput, "lossy payload is missing syntax flags"});
        }
        info.syntax_flags = payload[info.stream_offset++];
        info.adaptive_spans =
            (info.syntax_flags & kLossyTileSyntaxFlagAdaptiveSpanStreams) != 0;
        info.plane_extents =
            (info.syntax_flags & kLossyTileSyntaxFlagPlaneCoeffExtents) != 0;
        info.split_magnitude_signs =
            (info.syntax_flags & kLossyTileSyntaxFlagSplitMagnitudeSigns) != 0;
        info.shared_chroma_tables =
            (info.syntax_flags & kLossyTileSyntaxFlagSharedChromaCoeffTables) != 0;
    } else if (info.layout_tag == kLossyTileLayoutTagV5) {
        info.plane_extents = true;
    }

    return info;
}

size_t encoded_coefficient_span_stream_bytes(std::span<const uint8_t> payload,
                                             size_t offset,
                                             bool adaptive_spans,
                                             size_t expected_count) {
    if (!adaptive_spans) {
        return sizeof(uint16_t) + packed_coefficient_span_bytes(expected_count);
    }

    const uint16_t header = read_le16(payload.data() + offset);
    const uint16_t encoding = static_cast<uint16_t>(header & 0xC000u);
    const uint16_t encoded_bytes = static_cast<uint16_t>(header & 0x3FFFu);
    switch (encoding) {
    case 0x0000u:
    case 0x8000u:
        return sizeof(uint16_t) + encoded_bytes;
    case 0x4000u:
    default:
        return sizeof(uint16_t);
    }
}

}

TEST(RoundtripTest, LosslessSmallImage) {
    Image image(16, 16, BitDepth::Bits8, true);
    for (uint32_t y = 0; y < 16; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < 16; ++x) {
            row[x * 4 + 0] = static_cast<uint8_t>(x * 16);
            row[x * 4 + 1] = static_cast<uint8_t>(y * 16);
            row[x * 4 + 2] = static_cast<uint8_t>((x + y) * 8);
            row[x * 4 + 3] = 255;
        }
    }

    EncoderConfig config;
    config.lossless = true;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto decoded = decode(*encoded);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->width(), 16u);
    EXPECT_EQ(decoded->height(), 16u);
    EXPECT_EQ(image.pixels().size(), decoded->pixels().size());
    EXPECT_TRUE(std::equal(image.pixels().begin(), image.pixels().end(), decoded->pixels().begin()));
}

TEST(RoundtripTest, LossySolidColor) {
    Image image(64, 64, BitDepth::Bits8, true);
    for (uint32_t y = 0; y < 64; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < 64; ++x) {
            row[x * 4 + 0] = 128;
            row[x * 4 + 1] = 64;
            row[x * 4 + 2] = 192;
            row[x * 4 + 3] = 255;
        }
    }

    EncoderConfig config;
    config.quality = 90.0f;
    config.tile_size_log2 = 6;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto decoded = decode(*encoded);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->width(), 64u);
    EXPECT_EQ(decoded->height(), 64u);

    auto metrics = wk::metrics::compare_images(image, *decoded);
    ASSERT_TRUE(metrics.has_value()) << "metrics compare failed: " << metrics.error().message;
    EXPECT_GT(metrics->psnr, 25.0) << "PSNR too low: " << metrics->psnr;
    EXPECT_GT(metrics->ssim, 0.95) << "SSIM too low: " << metrics->ssim;
}

TEST(RoundtripTest, HigherLossyQualityTradesSizeForQuality) {
    Image image = make_rgb_gradient(192, 192);

    EncoderConfig config75;
    config75.quality = 75.0f;
    config75.subsampling = Subsampling::YUV444;

    EncoderConfig config85;
    config85.quality = 85.0f;
    config85.subsampling = Subsampling::YUV444;

    auto encoded75 = encode(image, config75);
    ASSERT_TRUE(encoded75.has_value()) << encoded75.error().message;
    auto decoded75 = decode(*encoded75);
    ASSERT_TRUE(decoded75.has_value()) << decoded75.error().message;
    auto metrics75 = wk::metrics::compare_images(image, *decoded75);
    ASSERT_TRUE(metrics75.has_value()) << "metrics compare failed: " << metrics75.error().message;

    auto encoded85 = encode(image, config85);
    ASSERT_TRUE(encoded85.has_value()) << encoded85.error().message;
    auto decoded85 = decode(*encoded85);
    ASSERT_TRUE(decoded85.has_value()) << decoded85.error().message;
    auto metrics85 = wk::metrics::compare_images(image, *decoded85);
    ASSERT_TRUE(metrics85.has_value()) << "metrics compare failed: " << metrics85.error().message;

    EXPECT_GT(encoded85->size(), encoded75->size());
    EXPECT_GT(metrics75->psnr, 46.0);
    EXPECT_GT(metrics75->ssim, 0.98);
    EXPECT_GT(metrics75->artifacts.chroma_psnr, 50.0);
    EXPECT_GE(metrics85->psnr, metrics75->psnr - 0.6) << "Higher quality should not materially regress PSNR";
    EXPECT_GE(metrics85->ssim, metrics75->ssim - 0.003) << "Higher quality should not materially regress SSIM";
    EXPECT_GE(metrics85->artifacts.chroma_psnr, metrics75->artifacts.chroma_psnr - 0.8)
        << "Higher quality should not materially regress chroma PSNR";
}

TEST(RoundtripTest, DefaultLossyTilePolicyUses1024ButLosslessStays512) {
    Image image = make_rgb_gradient(640, 384);

    EncoderConfig lossy_auto;
    lossy_auto.quality = 85.0f;
    lossy_auto.subsampling = Subsampling::YUV444;

    auto encoded_lossy = encode(image, lossy_auto);
    ASSERT_TRUE(encoded_lossy.has_value()) << encoded_lossy.error().message;

    auto lossy_info = get_info(*encoded_lossy);
    ASSERT_TRUE(lossy_info.has_value()) << lossy_info.error().message;
    EXPECT_EQ(lossy_info->tile_size, 1024u);

    auto lossy_file = parse_container(*encoded_lossy);
    ASSERT_TRUE(lossy_file.has_value()) << lossy_file.error().message;
    EXPECT_EQ(lossy_file->header.tile_size_log2, 10u);

    EncoderConfig lossy_512 = lossy_auto;
    lossy_512.tile_size_log2 = 9;

    auto encoded_lossy_512 = encode(image, lossy_512);
    ASSERT_TRUE(encoded_lossy_512.has_value()) << encoded_lossy_512.error().message;
    EXPECT_LT(encoded_lossy->size(), encoded_lossy_512->size());

    EncoderConfig lossless;
    lossless.lossless = true;

    auto encoded_lossless = encode(image, lossless);
    ASSERT_TRUE(encoded_lossless.has_value()) << encoded_lossless.error().message;

    auto lossless_info = get_info(*encoded_lossless);
    ASSERT_TRUE(lossless_info.has_value()) << lossless_info.error().message;
    EXPECT_EQ(lossless_info->tile_size, 512u);

    auto lossless_file = parse_container(*encoded_lossless);
    ASSERT_TRUE(lossless_file.has_value()) << lossless_file.error().message;
    EXPECT_EQ(lossless_file->header.tile_size_log2, 9u);
}

TEST(RoundtripTest, HigherLossyQualityImprovesDarkChromaStability) {
    Image image = make_dark_color_scene(192, 192);

    EncoderConfig config75;
    config75.quality = 75.0f;
    config75.subsampling = Subsampling::YUV444;

    EncoderConfig config85;
    config85.quality = 85.0f;
    config85.subsampling = Subsampling::YUV444;

    auto encoded75 = encode(image, config75);
    ASSERT_TRUE(encoded75.has_value()) << encoded75.error().message;
    auto decoded75 = decode(*encoded75);
    ASSERT_TRUE(decoded75.has_value()) << decoded75.error().message;
    auto metrics75 = wk::metrics::compare_images(image, *decoded75);
    ASSERT_TRUE(metrics75.has_value()) << "metrics compare failed: " << metrics75.error().message;

    auto encoded85 = encode(image, config85);
    ASSERT_TRUE(encoded85.has_value()) << encoded85.error().message;
    auto decoded85 = decode(*encoded85);
    ASSERT_TRUE(decoded85.has_value()) << decoded85.error().message;
    auto metrics85 = wk::metrics::compare_images(image, *decoded85);
    ASSERT_TRUE(metrics85.has_value()) << "metrics compare failed: " << metrics85.error().message;

    EXPECT_GT(encoded85->size(), encoded75->size());
    EXPECT_GT(metrics75->psnr, 24.0);
    EXPECT_GT(metrics75->ssim, 0.90);
    EXPECT_GT(metrics85->artifacts.chroma_psnr, metrics75->artifacts.chroma_psnr + 1.0)
        << "Higher quality should materially improve chroma fidelity in dark color scenes";
    EXPECT_LT(metrics85->artifacts.weighted_chroma_mae, metrics75->artifacts.weighted_chroma_mae)
        << "Higher quality should reduce weighted chroma error in dark color scenes";
}

TEST(RoundtripTest, LossyTransparentAlphaRoundTrip) {
    Image image(48, 48, BitDepth::Bits8, true);
    for (uint32_t y = 0; y < 48; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < 48; ++x) {
            row[x * 4 + 0] = static_cast<uint8_t>((x * 255u) / 47u);
            row[x * 4 + 1] = static_cast<uint8_t>((y * 255u) / 47u);
            row[x * 4 + 2] = static_cast<uint8_t>(((x + y) * 255u) / 94u);
            row[x * 4 + 3] = static_cast<uint8_t>(((x * y) * 255u) / (47u * 47u));
        }
    }

    EncoderConfig config;
    config.quality = 90.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto decoded = decode(*encoded);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_TRUE(decoded->has_alpha());
    EXPECT_EQ(decoded->width(), image.width());
    EXPECT_EQ(decoded->height(), image.height());

    auto metrics = wk::metrics::compare_images(image, *decoded);
    ASSERT_TRUE(metrics.has_value()) << "metrics compare failed: " << metrics.error().message;
    EXPECT_GT(metrics->channels[3].psnr, 24.0) << "Alpha PSNR too low: " << metrics->channels[3].psnr;
}

TEST(RoundtripTest, LossyAlphaTileMismatchRejected) {
    Image image(32, 32, BitDepth::Bits8, true);
    for (uint32_t y = 0; y < 32; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < 32; ++x) {
            row[x * 4 + 0] = 200;
            row[x * 4 + 1] = 40;
            row[x * 4 + 2] = 120;
            row[x * 4 + 3] = static_cast<uint8_t>((x * 255u) / 31u);
        }
    }

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_FALSE(file->tile_chunks.empty());
    ASSERT_GE(file->tile_chunks.front().payload.size(), 9u);
    file->tile_chunks.front().payload[4] = TILE_HAS_BASE;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::DecodeFailed);
}

TEST(RoundtripTest, BadEntropyStreamRejected) {
    Image image = make_rgb_gradient(64, 64);

    EncoderConfig config;
    config.lossless = true;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks[0].payload;
    const size_t stream_offset = 9u + 2u + 512u + 4u;
    ASSERT_GT(payload.size(), stream_offset + 3u);
    payload[stream_offset + 0] = 0;
    payload[stream_offset + 1] = 0;
    payload[stream_offset + 2] = 0;
    payload[stream_offset + 3] = 0;

    auto broken_file = write_container(*file);
    ASSERT_TRUE(broken_file.has_value()) << broken_file.error().message;

    auto decoded = decode(*broken_file);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::RansError);
}

TEST(RoundtripTest, PackedModeStreamCorruptionRejected) {
    Image image = make_rgb_gradient(24, 24);

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks.front().payload;
    auto syntax = parse_lossy_tile_syntax(payload);
    ASSERT_TRUE(syntax.has_value()) << syntax.error().message;
    const size_t mode_size_offset = syntax->stream_offset;

    ASSERT_GT(payload.size(), mode_size_offset + sizeof(uint16_t) - 1);
    payload[mode_size_offset + 0] = 0;
    payload[mode_size_offset + 1] = 0;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::PredictionError);
}

TEST(RoundtripTest, PackedCoefficientSpanCorruptionRejected) {
    Image image = make_rgb_gradient(24, 24);

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks.front().payload;
    auto syntax = parse_lossy_tile_syntax(payload);
    ASSERT_TRUE(syntax.has_value()) << syntax.error().message;
    const size_t y_blocks = 9;
    const size_t chroma_blocks = 9;
    const size_t y_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(y_blocks);
    const size_t chroma_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(chroma_blocks);
    const size_t span_size_offset = syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes;

    ASSERT_GT(payload.size(), span_size_offset + sizeof(uint16_t) - 1);
    payload[span_size_offset + 0] = 0;
    payload[span_size_offset + 1] = 0xC0;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::DecodeFailed);
}

TEST(RoundtripTest, CoefficientSignCorruptionRejected) {
    Image image = make_rgb_gradient(24, 24);

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks.front().payload;
    auto syntax = parse_lossy_tile_syntax(payload);
    ASSERT_TRUE(syntax.has_value()) << syntax.error().message;
    ASSERT_TRUE(syntax->split_magnitude_signs);

    const size_t y_blocks = 9;
    const size_t chroma_blocks = 9;
    ByteReader reader(std::span<const uint8_t>(payload.data() + syntax->stream_offset,
                                               payload.size() - syntax->stream_offset));

    auto y_modes = read_packed_prediction_modes(reader, y_blocks, "luma");
    ASSERT_TRUE(y_modes.has_value()) << y_modes.error().message;
    auto chroma_modes = read_packed_prediction_modes(reader, chroma_blocks, "chroma");
    ASSERT_TRUE(chroma_modes.has_value()) << chroma_modes.error().message;

    auto y_spans = syntax->adaptive_spans
        ? read_adaptive_coefficient_spans(reader, y_blocks, "luma")
        : read_packed_coefficient_spans(reader, y_blocks, "luma");
    ASSERT_TRUE(y_spans.has_value()) << y_spans.error().message;
    auto chroma_spans = syntax->adaptive_spans
        ? read_adaptive_coefficient_spans(reader, chroma_blocks, "chroma")
        : read_packed_coefficient_spans(reader, chroma_blocks, "chroma");
    ASSERT_TRUE(chroma_spans.has_value()) << chroma_spans.error().message;

    if (syntax->plane_extents) {
        auto y_extent = reader.read_u8();
        auto chroma_extent = reader.read_u8();
        ASSERT_TRUE(y_extent.has_value()) << y_extent.error().message;
        ASSERT_TRUE(chroma_extent.has_value()) << chroma_extent.error().message;
    }

    auto table = read_coefficient_table(reader, 1025, "lossy");
    ASSERT_TRUE(table.has_value()) << table.error().message;

    auto encoded_size = reader.read_u32();
    ASSERT_TRUE(encoded_size.has_value()) << encoded_size.error().message;
    auto encoded_bytes = reader.read_bytes(*encoded_size);
    ASSERT_TRUE(encoded_bytes.has_value()) << encoded_bytes.error().message;

    RansDecoder<RANS_PRECISION_BITS> decoder;
    decoder.init(encoded_bytes->data(), encoded_bytes->size());
    ASSERT_TRUE(decoder.ok());

    size_t nonzero_count = 0;
    for (size_t block_index = 0; block_index < y_blocks; ++block_index) {
        if ((*y_spans)[block_index] == 0) {
            continue;
        }
        const int symbol = decoder.decode(*table);
        ASSERT_TRUE(decoder.ok());
        nonzero_count += symbol != 0 ? 1u : 0u;
    }

    ASSERT_GT(nonzero_count, 0u);
    ASSERT_NE(nonzero_count % 8u, 0u);

    const size_t sign_offset = syntax->stream_offset + reader.position();
    const size_t sign_bytes = packed_coefficient_sign_bytes(nonzero_count);
    ASSERT_GT(payload.size(), sign_offset + sign_bytes - 1);
    payload[sign_offset + sign_bytes - 1] |= 0x80;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::DecodeFailed);
}

TEST(RoundtripTest, PlaneCoefficientExtentCorruptionRejected) {
    Image image(24, 24, BitDepth::Bits8, false);
    for (auto& byte : image.pixels()) {
        byte = 96;
    }

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks.front().payload;
    auto syntax = parse_lossy_tile_syntax(payload);
    ASSERT_TRUE(syntax.has_value()) << syntax.error().message;
    const size_t y_blocks = 9;
    const size_t chroma_blocks = 9;
    const size_t y_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(y_blocks);
    const size_t chroma_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(chroma_blocks);
    const size_t y_span_stream_bytes = encoded_coefficient_span_stream_bytes(
        payload, syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes,
        syntax->adaptive_spans, y_blocks);
    const size_t chroma_span_stream_bytes = encoded_coefficient_span_stream_bytes(
        payload, syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes + y_span_stream_bytes,
        syntax->adaptive_spans, chroma_blocks);
    const size_t plane_extent_offset = syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes +
                                       y_span_stream_bytes + chroma_span_stream_bytes;

    ASSERT_TRUE(syntax->plane_extents);
    ASSERT_GT(payload.size(), plane_extent_offset);
    payload[plane_extent_offset] = 0;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::DecodeFailed);
}

TEST(RoundtripTest, CoefficientTableCorruptionRejected) {
    Image image = make_rgb_gradient(24, 24);

    EncoderConfig config;
    config.quality = 85.0f;
    config.subsampling = Subsampling::YUV444;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value()) << encoded.error().message;

    auto file = parse_container(*encoded);
    ASSERT_TRUE(file.has_value()) << file.error().message;
    ASSERT_EQ(file->tile_chunks.size(), 1u);

    auto& payload = file->tile_chunks.front().payload;
    auto syntax = parse_lossy_tile_syntax(payload);
    ASSERT_TRUE(syntax.has_value()) << syntax.error().message;
    const size_t y_blocks = 9;
    const size_t chroma_blocks = 9;
    const size_t y_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(y_blocks);
    const size_t chroma_mode_stream_bytes = sizeof(uint16_t) + packed_prediction_mode_bytes(chroma_blocks);
    const size_t y_span_stream_bytes = encoded_coefficient_span_stream_bytes(
        payload, syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes,
        syntax->adaptive_spans, y_blocks);
    const size_t chroma_span_stream_bytes = encoded_coefficient_span_stream_bytes(
        payload, syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes + y_span_stream_bytes,
        syntax->adaptive_spans, chroma_blocks);
    const size_t plane_extent_bytes = syntax->plane_extents ? 2u : 0u;
    const size_t coeff_table_offset = syntax->stream_offset + y_mode_stream_bytes + chroma_mode_stream_bytes +
                                      y_span_stream_bytes + chroma_span_stream_bytes + plane_extent_bytes;

    ASSERT_GT(payload.size(), coeff_table_offset);
    payload[coeff_table_offset] = 0xFF;

    auto broken = write_container(*file);
    ASSERT_TRUE(broken.has_value()) << broken.error().message;

    auto decoded = decode(*broken);
    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::RansError);
}

TEST(RoundtripTest, PhotoJpegPipelines) {
#ifndef _WIN32
    GTEST_SKIP() << "JPEG integration test requires Windows WIC in this build";
#else
    const std::filesystem::path photo = sample_photo_path();
    if (!std::filesystem::exists(photo)) {
        GTEST_SKIP() << "Sample photo not found: " << photo.string();
    }

    auto source = wk::io::load_image_file(photo.string());
    ASSERT_TRUE(source.has_value()) << source.error().message;
    EXPECT_FALSE(source->has_alpha());

    EncoderConfig lossless_config;
    lossless_config.lossless = true;

    auto encoded_lossless = encode(*source, lossless_config);
    ASSERT_TRUE(encoded_lossless.has_value()) << encoded_lossless.error().message;
    auto decoded_lossless = decode(*encoded_lossless);
    ASSERT_TRUE(decoded_lossless.has_value()) << decoded_lossless.error().message;
    EXPECT_EQ(decoded_lossless->width(), source->width());
    EXPECT_EQ(decoded_lossless->height(), source->height());
    EXPECT_TRUE(std::equal(decoded_lossless->pixels().begin(), decoded_lossless->pixels().end(), source->pixels().begin()));

    EncoderConfig lossy_config;
    lossy_config.quality = 75.0f;

    auto encoded_lossy = encode(*source, lossy_config);
    ASSERT_TRUE(encoded_lossy.has_value()) << encoded_lossy.error().message;
    auto decoded_lossy = decode(*encoded_lossy);
    ASSERT_TRUE(decoded_lossy.has_value()) << decoded_lossy.error().message;
    EXPECT_EQ(decoded_lossy->width(), source->width());
    EXPECT_EQ(decoded_lossy->height(), source->height());

    auto metrics = wk::metrics::compare_images(*source, *decoded_lossy);
    ASSERT_TRUE(metrics.has_value()) << "metrics compare failed: " << metrics.error().message;
    EXPECT_GT(metrics->psnr, 30.0) << "Photo JPEG lossy PSNR too low: " << metrics->psnr;
    EXPECT_GT(metrics->ssim, 0.90) << "Photo JPEG lossy SSIM too low: " << metrics->ssim;
#endif
}

TEST(RoundtripTest, GetInfo) {
    Image image(320, 240, BitDepth::Bits8, true);
    for (auto& byte : image.pixels()) {
        byte = 128;
    }

    EncoderConfig config;
    config.lossless = true;

    auto encoded = encode(image, config);
    ASSERT_TRUE(encoded.has_value());

    auto info = get_info(*encoded);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->width, 320u);
    EXPECT_EQ(info->height, 240u);
    EXPECT_EQ(info->bit_depth, BitDepth::Bits8);
    EXPECT_TRUE(info->is_lossless);
    EXPECT_TRUE(info->has_alpha);
}

TEST(RoundtripTest, EmptyInputError) {
    auto result = decode({});
    EXPECT_FALSE(result.has_value());
}

TEST(RoundtripTest, ZeroDimensionError) {
    Image image(0, 0, BitDepth::Bits8, false);
    auto result = encode(image);
    EXPECT_FALSE(result.has_value());
}




