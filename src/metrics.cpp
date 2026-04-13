#include "metrics.h"
#include <algorithm>
#include <cmath>

namespace wk::metrics {

namespace {

uint32_t max_sample_value(BitDepth bit_depth) {
    switch (bit_depth) {
    case BitDepth::Bits8:
        return 255u;
    case BitDepth::Bits10:
        return 1023u;
    case BitDepth::Bits12:
        return 4095u;
    }
    return 255u;
}

uint8_t channel_count(const Image& image) {
    return image.has_alpha() ? 4u : 3u;
}

double sample_at(const Image& image, size_t pixel_index, uint8_t channel_index) {
    const auto pixels = image.pixels();
    const size_t channels = channel_count(image);
    if (image.bit_depth() == BitDepth::Bits8) {
        const size_t offset = pixel_index * channels + channel_index;
        return static_cast<double>(pixels[offset]);
    }

    const size_t offset = (pixel_index * channels + channel_index) * 2;
    return static_cast<double>(read_le16(pixels.data() + offset));
}

double psnr_from_mse(double mse, double max_value) {
    if (mse <= 1e-12) {
        return 100.0;
    }
    return 10.0 * std::log10((max_value * max_value) / mse);
}

double compute_channel_ssim(const Image& reference, const Image& candidate, uint8_t channel_index, double max_value) {
    constexpr uint32_t window_size = 8;
    const double c1 = std::pow(0.01 * max_value, 2.0);
    const double c2 = std::pow(0.03 * max_value, 2.0);
    double total_ssim = 0.0;
    uint32_t window_count = 0;

    for (uint32_t y = 0; y < reference.height(); y += window_size) {
        const uint32_t win_h = std::min(window_size, reference.height() - y);
        for (uint32_t x = 0; x < reference.width(); x += window_size) {
            const uint32_t win_w = std::min(window_size, reference.width() - x);
            double sum_x = 0.0;
            double sum_y = 0.0;
            double sum_x2 = 0.0;
            double sum_y2 = 0.0;
            double sum_xy = 0.0;

            for (uint32_t wy = 0; wy < win_h; ++wy) {
                for (uint32_t wx = 0; wx < win_w; ++wx) {
                    const size_t pixel_index = static_cast<size_t>(y + wy) * reference.width() + (x + wx);
                    const double ref_sample = sample_at(reference, pixel_index, channel_index);
                    const double cand_sample = sample_at(candidate, pixel_index, channel_index);
                    sum_x += ref_sample;
                    sum_y += cand_sample;
                    sum_x2 += ref_sample * ref_sample;
                    sum_y2 += cand_sample * cand_sample;
                    sum_xy += ref_sample * cand_sample;
                }
            }

            const double count = static_cast<double>(win_w) * win_h;
            const double mean_x = sum_x / count;
            const double mean_y = sum_y / count;
            const double var_x = std::max(0.0, (sum_x2 / count) - (mean_x * mean_x));
            const double var_y = std::max(0.0, (sum_y2 / count) - (mean_y * mean_y));
            const double cov_xy = (sum_xy / count) - (mean_x * mean_y);
            const double numerator = (2.0 * mean_x * mean_y + c1) * (2.0 * cov_xy + c2);
            const double denominator = (mean_x * mean_x + mean_y * mean_y + c1) * (var_x + var_y + c2);
            total_ssim += denominator > 0.0 ? numerator / denominator : 1.0;
            ++window_count;
        }
    }

    return window_count > 0 ? total_ssim / window_count : 1.0;
}

ChannelMetrics compute_channel_metrics(const Image& reference, const Image& candidate, uint8_t channel_index, double max_value) {
    const size_t pixel_count = static_cast<size_t>(reference.width()) * reference.height();
    double sum_abs = 0.0;
    double sum_sq = 0.0;

    for (size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
        const double ref_sample = sample_at(reference, pixel_index, channel_index);
        const double cand_sample = sample_at(candidate, pixel_index, channel_index);
        const double delta = ref_sample - cand_sample;
        sum_abs += std::abs(delta);
        sum_sq += delta * delta;
    }

    ChannelMetrics metrics;
    if (pixel_count > 0) {
        metrics.mae = sum_abs / static_cast<double>(pixel_count);
        metrics.mse = sum_sq / static_cast<double>(pixel_count);
    }
    metrics.psnr = psnr_from_mse(metrics.mse, max_value);
    metrics.ssim = compute_channel_ssim(reference, candidate, channel_index, max_value);
    return metrics;
}

}

Result<ImageMetrics> compare_images(const Image& reference, const Image& candidate, bool include_alpha) {
    if (reference.width() == 0 || reference.height() == 0 || candidate.width() == 0 || candidate.height() == 0) {
        return std::unexpected(Error{ErrorCode::InvalidParameter, "cannot compare empty images"});
    }
    if (reference.width() != candidate.width() || reference.height() != candidate.height()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter, "image dimensions do not match"});
    }
    if (reference.bit_depth() != candidate.bit_depth()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter, "image bit depths do not match"});
    }

    ImageMetrics result;
    result.width = reference.width();
    result.height = reference.height();
    result.compared_channels = static_cast<uint8_t>((include_alpha && reference.has_alpha() && candidate.has_alpha()) ? 4 : 3);
    result.compared_alpha = result.compared_channels == 4;

    const double max_value = static_cast<double>(max_sample_value(reference.bit_depth()));
    const size_t pixel_count = static_cast<size_t>(reference.width()) * reference.height();
    double total_abs = 0.0;
    double total_sq = 0.0;
    double total_ssim = 0.0;

    for (uint8_t channel_index = 0; channel_index < result.compared_channels; ++channel_index) {
        result.channels[channel_index] = compute_channel_metrics(reference, candidate, channel_index, max_value);
        total_abs += result.channels[channel_index].mae * static_cast<double>(pixel_count);
        total_sq += result.channels[channel_index].mse * static_cast<double>(pixel_count);
        total_ssim += result.channels[channel_index].ssim;
    }

    const double sample_count = static_cast<double>(pixel_count) * result.compared_channels;
    result.mae = total_abs / sample_count;
    result.mse = total_sq / sample_count;
    result.psnr = psnr_from_mse(result.mse, max_value);
    result.ssim = total_ssim / result.compared_channels;
    return result;
}

}