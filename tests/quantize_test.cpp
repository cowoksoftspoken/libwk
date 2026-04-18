#include <gtest/gtest.h>
#include "../src/quantize.h"

using namespace wk;

TEST(QuantizeTest, UsesNaturalMatrixInZigzagOrder) {
    QuantTable quant;
    quant.build(50.0f, false, 8);

    const auto& steps = quant.steps();
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(steps[i], BASE_QUANT_LUMA[ZIGZAG_ORDER[i]])
            << "Unexpected quant step at zigzag index " << i;
    }
}

TEST(QuantizeTest, BitDepthScalingPreservesZigzagMapping) {
    QuantTable quant;
    quant.build(50.0f, false, 10);

    const auto& steps = quant.steps();
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(steps[i], BASE_QUANT_LUMA[ZIGZAG_ORDER[i]] * 4)
            << "Unexpected 10-bit quant step at zigzag index " << i;
    }
}
