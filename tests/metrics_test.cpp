#include <gtest/gtest.h>
#include <wk/wk.hpp>
#include "../src/metrics.h"

using namespace wk;

namespace {

Image make_rgb_image(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b) {
    Image image(width, height, BitDepth::Bits8, false);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < width; ++x) {
            row[x * 3 + 0] = r;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = b;
        }
    }
    return image;
}

}

TEST(MetricsTest, IdenticalImageIsPerfect) {
    Image reference = make_rgb_image(16, 16, 90, 120, 200);
    auto metrics = wk::metrics::compare_images(reference, reference);
    ASSERT_TRUE(metrics.has_value()) << metrics.error().message;
    EXPECT_DOUBLE_EQ(metrics->mae, 0.0);
    EXPECT_DOUBLE_EQ(metrics->mse, 0.0);
    EXPECT_DOUBLE_EQ(metrics->psnr, 100.0);
    EXPECT_NEAR(metrics->ssim, 1.0, 1e-9);
}

TEST(MetricsTest, AlphaIsIgnoredWhenOnlyOneImageHasIt) {
    Image reference(8, 8, BitDepth::Bits8, true);
    Image candidate(8, 8, BitDepth::Bits8, false);

    for (uint32_t y = 0; y < 8; ++y) {
        uint8_t* ref_row = reference.row(y);
        uint8_t* cand_row = candidate.row(y);
        for (uint32_t x = 0; x < 8; ++x) {
            ref_row[x * 4 + 0] = 10;
            ref_row[x * 4 + 1] = 20;
            ref_row[x * 4 + 2] = 30;
            ref_row[x * 4 + 3] = static_cast<uint8_t>(x * 8);
            cand_row[x * 3 + 0] = 10;
            cand_row[x * 3 + 1] = 20;
            cand_row[x * 3 + 2] = 30;
        }
    }

    auto metrics = wk::metrics::compare_images(reference, candidate);
    ASSERT_TRUE(metrics.has_value()) << metrics.error().message;
    EXPECT_EQ(metrics->compared_channels, 3u);
    EXPECT_FALSE(metrics->compared_alpha);
    EXPECT_DOUBLE_EQ(metrics->mae, 0.0);
}

TEST(MetricsTest, DimensionMismatchRejected) {
    Image reference = make_rgb_image(16, 16, 1, 2, 3);
    Image candidate = make_rgb_image(15, 16, 1, 2, 3);
    auto metrics = wk::metrics::compare_images(reference, candidate);
    ASSERT_FALSE(metrics.has_value());
    EXPECT_EQ(metrics.error().code, ErrorCode::InvalidParameter);
}