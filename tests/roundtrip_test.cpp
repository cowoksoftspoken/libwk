
#include <gtest/gtest.h>
#include <algorithm>
#include <wk/wk.hpp>
#include "../src/common.h"
#include "../src/container.h"
#include "../src/image_io.h"
#include <cmath>
#include <filesystem>

using namespace wk;

namespace {

double compute_psnr(const uint8_t* a, const uint8_t* b, size_t count, int max_val = 255) {
    double mse = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        mse += diff * diff;
    }
    mse /= static_cast<double>(count);
    if (mse < 1e-10) {
        return 100.0;
    }
    return 10.0 * std::log10(static_cast<double>(max_val * max_val) / mse);
}

double compute_channel_psnr(const uint8_t* a, const uint8_t* b, size_t pixel_count, size_t channel_index, size_t channels, int max_val = 255) {
    double mse = 0.0;
    for (size_t i = 0; i < pixel_count; ++i) {
        const double diff = static_cast<double>(a[i * channels + channel_index]) -
                            static_cast<double>(b[i * channels + channel_index]);
        mse += diff * diff;
    }
    mse /= static_cast<double>(pixel_count);
    if (mse < 1e-10) {
        return 100.0;
    }
    return 10.0 * std::log10(static_cast<double>(max_val * max_val) / mse);
}

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

    const double psnr = compute_psnr(image.pixels().data(), decoded->pixels().data(), image.pixels().size());
    EXPECT_GT(psnr, 25.0) << "PSNR too low: " << psnr;
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

    const size_t pixel_count = static_cast<size_t>(image.width()) * image.height();
    const double alpha_psnr = compute_channel_psnr(image.pixels().data(), decoded->pixels().data(), pixel_count, 3, 4);
    EXPECT_GT(alpha_psnr, 24.0) << "Alpha PSNR too low: " << alpha_psnr;
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

    const double psnr = compute_psnr(source->pixels().data(), decoded_lossy->pixels().data(), source->pixels().size());
    EXPECT_GT(psnr, 20.0) << "Photo JPEG lossy PSNR too low: " << psnr;
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


