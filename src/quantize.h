#pragma once

#include "common.h"
#include "dct.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace wk {

constexpr std::array<uint16_t, 64> BASE_QUANT_LUMA = {{
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,
    24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101,
    72, 92, 95, 98,112,100,103, 99
}};

constexpr std::array<uint16_t, 64> BASE_QUANT_CHROMA = {{
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
}};

class QuantTable {
public:
    QuantTable() = default;

    void build(float quality, bool is_chroma, uint8_t bit_depth) {
        quality_ = std::clamp(quality, 1.0f, 100.0f);
        is_chroma_ = is_chroma;
        const auto& base = is_chroma ? BASE_QUANT_CHROMA : BASE_QUANT_LUMA;

        const float q = quality_;
        float scale;
        if (q < 50.0f) {
            scale = 5000.0f / q;
        } else {
            scale = 200.0f - 2.0f * q;
        }
        scale = std::max(scale, 1.0f);

        float bd_mul = 1.0f;
        if (bit_depth == 10) bd_mul = 4.0f;
        else if (bit_depth == 12) bd_mul = 16.0f;

        for (int i = 0; i < 64; i++) {
            float base_value = static_cast<float>(base[i]);
            if (is_chroma_) {
                base_value *= chroma_step_scale(i);
            }
            const float v = (base_value * scale / 100.0f) * bd_mul;
            table_[i] = static_cast<uint16_t>(std::max(v, 1.0f));
            inv_table_[i] = 1.0f / table_[i];
        }
    }

    void quantize(const DctBlock& input, DctBlockI16& output) const {
        for (int i = 0; i < 64; i++) {
            const int zi = ZIGZAG_ORDER[i];
            const float v = input[zi];
            const float step = static_cast<float>(table_[i]);
            if (std::abs(v) < step * deadzone_scale(i)) {
                output[i] = 0;
            } else {
                const float q = v * inv_table_[i];
                output[i] = static_cast<int16_t>(
                    (v > 0) ? std::floor(q + 0.5f) : std::ceil(q - 0.5f));
            }
        }
    }

    void dequantize(const DctBlockI16& input, DctBlock& output) const {
        for (int i = 0; i < 64; i++) {
            const int zi = ZIGZAG_ORDER[i];
            output[zi] = static_cast<float>(input[i]) * table_[i];
        }
    }

    [[nodiscard]] uint16_t step(int i) const { return table_[i]; }
    [[nodiscard]] const std::array<uint16_t, 64>& steps() const { return table_; }

private:
    [[nodiscard]] float chroma_step_scale(int zigzag_index) const {
        if (!is_chroma_) {
            return 1.0f;
        }

        if (quality_ >= 90.0f) {
            if (zigzag_index == 0) return 0.68f;
            if (zigzag_index < 8) return 0.62f;
            if (zigzag_index < 24) return 0.58f;
            return 0.70f;
        }
        if (quality_ >= 85.0f) {
            if (zigzag_index == 0) return 0.74f;
            if (zigzag_index < 8) return 0.68f;
            if (zigzag_index < 24) return 0.64f;
            return 0.76f;
        }
        if (quality_ >= 75.0f) {
            if (zigzag_index == 0) return 0.82f;
            if (zigzag_index < 8) return 0.76f;
            if (zigzag_index < 24) return 0.72f;
            return 0.84f;
        }

        if (zigzag_index == 0) return 0.90f;
        if (zigzag_index < 8) return 0.86f;
        if (zigzag_index < 24) return 0.82f;
        return 0.92f;
    }

    [[nodiscard]] float deadzone_scale(int zigzag_index) const {
        if (zigzag_index == 0) {
            return is_chroma_ ? 0.46f : 0.5f;
        }

        float scale = is_chroma_ ? 0.49f : 0.52f;
        if (quality_ >= 85.0f) {
            scale += is_chroma_ ? 0.0f : 0.03f;
        } else if (quality_ >= 75.0f) {
            scale += is_chroma_ ? 0.01f : 0.01f;
        }

        if (zigzag_index >= 20) {
            scale += is_chroma_ ? 0.04f : 0.06f;
        } else if (zigzag_index >= 8) {
            scale += is_chroma_ ? 0.02f : 0.03f;
        }

        return std::clamp(scale, is_chroma_ ? 0.46f : 0.5f, 0.7f);
    }

    std::array<uint16_t, 64> table_{};
    std::array<float, 64>    inv_table_{};
    float                    quality_ = 75.0f;
    bool                     is_chroma_ = false;
};

inline int quality_to_qp(float quality) {
    return static_cast<int>(100.0f - std::clamp(quality, 0.0f, 100.0f));
}

inline float qp_to_lambda(int qp) {
    const float q = static_cast<float>(qp);
    return 0.85f * std::pow(2.0f, (q - 12.0f) / 3.0f);
}

}
