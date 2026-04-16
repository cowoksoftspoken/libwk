#pragma once

#include "common.h"
#include <wk/wk.hpp>
#include <array>

namespace wk::metrics {

struct ChannelMetrics {
    double mae = 0.0;
    double mse = 0.0;
    double psnr = 100.0;
    double ssim = 1.0;
};

struct ArtifactMetrics {
    double chroma_mae = 0.0;
    double chroma_mse = 0.0;
    double chroma_psnr = 100.0;
    double weighted_luma_mae = 0.0;
    double weighted_chroma_mae = 0.0;
    double max_abs_error = 0.0;
};

struct ImageMetrics {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t compared_channels = 0;
    bool compared_alpha = false;
    double mae = 0.0;
    double mse = 0.0;
    double psnr = 100.0;
    double ssim = 1.0;
    std::array<ChannelMetrics, 4> channels{};
    std::array<ChannelMetrics, 3> ycbcr{};
    ArtifactMetrics artifacts{};
};

Result<ImageMetrics> compare_images(const Image& reference, const Image& candidate, bool include_alpha = true);

}
