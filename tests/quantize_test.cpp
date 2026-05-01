// Copyright 2026 Inggrit Setya Budi
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
