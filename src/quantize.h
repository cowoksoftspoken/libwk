#pragma once


#include "common.h"
#include "dct.h"
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
        const auto& base = is_chroma ? BASE_QUANT_CHROMA : BASE_QUANT_LUMA;


        float q = std::clamp(quality, 1.0f, 100.0f);
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
            float v = (base[i] * scale / 100.0f) * bd_mul;
            table_[i] = static_cast<uint16_t>(std::max(v, 1.0f));
            inv_table_[i] = 1.0f / table_[i];
        }
    }


    void quantize(const DctBlock& input, DctBlockI16& output) const {
        for (int i = 0; i < 64; i++) {
            int zi = ZIGZAG_ORDER[i];
            float v = input[zi];

            float step = static_cast<float>(table_[i]);
            if (std::abs(v) < step * 0.5f) {
                output[i] = 0;
            } else {
                float q = v * inv_table_[i];
                output[i] = static_cast<int16_t>(
                    (v > 0) ? std::floor(q + 0.5f) : std::ceil(q - 0.5f));
            }
        }
    }


    void dequantize(const DctBlockI16& input, DctBlock& output) const {
        for (int i = 0; i < 64; i++) {
            int zi = ZIGZAG_ORDER[i];
            output[zi] = static_cast<float>(input[i]) * table_[i];
        }
    }

    [[nodiscard]] uint16_t step(int i) const { return table_[i]; }
    [[nodiscard]] const std::array<uint16_t, 64>& steps() const { return table_; }

private:
    std::array<uint16_t, 64> table_{};
    std::array<float, 64>    inv_table_{};
};




inline int quality_to_qp(float quality) {
    return static_cast<int>(100.0f - std::clamp(quality, 0.0f, 100.0f));
}



inline float qp_to_lambda(int qp) {
    float q = static_cast<float>(qp);
    return 0.85f * std::pow(2.0f, (q - 12.0f) / 3.0f);
}

}
