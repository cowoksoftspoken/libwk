
#include <gtest/gtest.h>
#include <algorithm>
#include <wk/wk.hpp>
#include "../src/common.h"
#include "../src/container.h"
#include "../src/image_io.h"
#include "../src/metrics.h"
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

std::filesystem::path sample_photo_path() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "photos" / "1230927884722.jpg";
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
    EXPECT_GT(metrics85->psnr, metrics75->psnr + 0.5) << "Higher quality should increase PSNR";
    EXPECT_GT(metrics85->ssim, metrics75->ssim) << "Higher quality should increase SSIM";
    EXPECT_GT(metrics85->artifacts.chroma_psnr, metrics75->artifacts.chroma_psnr) << "Higher quality should increase chroma PSNR";
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




