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
#include "../src/predict.h"
#include <cstring>

using namespace wk;

class PredictTest : public ::testing::Test {
protected:
    int16_t above[8];
    int16_t left[8];
    int16_t pred[64];
    int16_t above_left = 128;

    void SetUp() override {
        for (int i = 0; i < 8; i++) {
            above[i] = static_cast<int16_t>(100 + i * 5);
            left[i] = static_cast<int16_t>(80 + i * 10);
        }
    }
};

TEST_F(PredictTest, DcMode) {
    predict_8x8(PredMode::DC, above, left, above_left, pred, 255);


    int sum = 0;
    for (int i = 0; i < 8; i++) sum += above[i] + left[i];
    int16_t expected = static_cast<int16_t>((sum + 8) / 16);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(pred[i], expected) << "at " << i;
    }
}

TEST_F(PredictTest, VerticalMode) {
    predict_8x8(PredMode::V, above, left, above_left, pred, 255);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            EXPECT_EQ(pred[r * 8 + c], above[c])
                << "at (" << r << "," << c << ")";
        }
    }
}

TEST_F(PredictTest, HorizontalMode) {
    predict_8x8(PredMode::H, above, left, above_left, pred, 255);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            EXPECT_EQ(pred[r * 8 + c], left[r])
                << "at (" << r << "," << c << ")";
        }
    }
}

TEST_F(PredictTest, TrueMotionMode) {
    predict_8x8(PredMode::TM, above, left, above_left, pred, 255);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int expected = left[r] + above[c] - above_left;
            expected = std::clamp(expected, 0, 255);
            EXPECT_EQ(pred[r * 8 + c], expected)
                << "at (" << r << "," << c << ")";
        }
    }
}

TEST_F(PredictTest, DC128Mode) {
    predict_8x8(PredMode::DC_128, above, left, above_left, pred, 255);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(pred[i], 128) << "at " << i;
    }
}

TEST_F(PredictTest, DC128Mode10Bit) {
    predict_8x8(PredMode::DC_128, above, left, above_left, pred, 1023);

    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(pred[i], 512) << "at " << i;
    }
}

TEST_F(PredictTest, NullNeighbors) {

    predict_8x8(PredMode::DC, nullptr, nullptr, 0, pred, 255);


    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(pred[i], 128) << "at " << i;
    }
}

TEST_F(PredictTest, ModeSelectionPicksBest) {

    int16_t original[64];
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            original[r * 8 + c] = above[c];
        }
    }

    auto result = select_best_mode(original, above, left, above_left, 1.0f, 255);
    EXPECT_EQ(result.mode, PredMode::V);
}

TEST_F(PredictTest, ModeSelectionHorizontal) {

    int16_t original[64];
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            original[r * 8 + c] = left[r];
        }
    }

    auto result = select_best_mode(original, above, left, above_left, 1.0f, 255);
    EXPECT_EQ(result.mode, PredMode::H);
}

TEST_F(PredictTest, DeterministicModeNoNeighborsUsesDc128) {
    EXPECT_EQ(select_deterministic_mode(nullptr, nullptr, above_left, 255), PredMode::DC_128);
}

TEST_F(PredictTest, DeterministicModeSingleEdgeFollowsDirection) {
    EXPECT_EQ(select_deterministic_mode(above, nullptr, above_left, 255), PredMode::V);
    EXPECT_EQ(select_deterministic_mode(nullptr, left, above_left, 255), PredMode::H);
}

TEST_F(PredictTest, DeterministicModeSmoothCornerUsesTrueMotion) {
    int16_t smooth_above[8] = {130, 131, 131, 132, 132, 133, 133, 134};
    int16_t smooth_left[8] = {130, 130, 131, 131, 132, 132, 133, 133};
    EXPECT_EQ(select_deterministic_mode(smooth_above, smooth_left, 130, 255), PredMode::TM);
}

TEST_F(PredictTest, DeterministicModeDisjointEdgesUsesDc) {
    int16_t flat_above[8] = {200, 200, 200, 200, 200, 200, 200, 200};
    int16_t flat_left[8] = {40, 40, 40, 40, 40, 40, 40, 40};
    EXPECT_EQ(select_deterministic_mode(flat_above, flat_left, 128, 255), PredMode::DC);
}

TEST_F(PredictTest, AllModesProduceValid) {
    for (int m = 0; m < static_cast<int>(PredMode::NUM_MODES); m++) {
        predict_8x8(static_cast<PredMode>(m), above, left, above_left, pred, 255);

        for (int i = 0; i < 64; i++) {
            EXPECT_GE(pred[i], 0) << "Mode " << m << " at " << i;
            EXPECT_LE(pred[i], 255) << "Mode " << m << " at " << i;
        }
    }
}
