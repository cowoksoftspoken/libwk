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
};

Result<ImageMetrics> compare_images(const Image& reference, const Image& candidate, bool include_alpha = true);

}