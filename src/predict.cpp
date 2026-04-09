
#include "predict.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

namespace wk {

static inline int16_t clamp_px(int v, int16_t max_val) {
    return static_cast<int16_t>(std::clamp(v, 0, static_cast<int>(max_val)));
}

void predict_8x8(PredMode mode, const int16_t* above, const int16_t* left,
                  int16_t above_left, int16_t* pred, int16_t max_val) {

    int16_t default_above[8], default_left[8];
    int16_t mid = static_cast<int16_t>((static_cast<int>(max_val) + 1) / 2);

    if (!above) {
        std::fill(default_above, default_above + 8, mid);
        above = default_above;
    }
    if (!left) {
        std::fill(default_left, default_left + 8, mid);
        left = default_left;
    }

    switch (mode) {
    case PredMode::DC: {
        int sum = 0;
        for (int i = 0; i < 8; i++) sum += above[i] + left[i];
        int16_t dc = static_cast<int16_t>((sum + 8) / 16);
        std::fill(pred, pred + 64, dc);
        break;
    }
    case PredMode::V: {
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                pred[r * 8 + c] = above[c];
            }
        }
        break;
    }
    case PredMode::H: {
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                pred[r * 8 + c] = left[r];
            }
        }
        break;
    }
    case PredMode::TM: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int v = static_cast<int>(left[r]) + static_cast<int>(above[c])
                        - static_cast<int>(above_left);
                pred[r * 8 + c] = clamp_px(v, max_val);
            }
        }
        break;
    }
    case PredMode::DC_LEFT: {
        int sum = 0;
        for (int i = 0; i < 8; i++) sum += left[i];
        int16_t dc = static_cast<int16_t>((sum + 4) / 8);
        std::fill(pred, pred + 64, dc);
        break;
    }
    case PredMode::DC_TOP: {
        int sum = 0;
        for (int i = 0; i < 8; i++) sum += above[i];
        int16_t dc = static_cast<int16_t>((sum + 4) / 8);
        std::fill(pred, pred + 64, dc);
        break;
    }
    case PredMode::DC_128: {
        std::fill(pred, pred + 64, mid);
        break;
    }
    case PredMode::D45: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = c + r + 1;
                if (idx < 8) {
                    pred[r * 8 + c] = above[idx];
                } else {
                    pred[r * 8 + c] = above[7];
                }
            }
        }
        break;
    }
    case PredMode::D135: {


        int16_t ref[17];
        for (int i = 0; i < 8; i++) ref[i] = left[7 - i];
        ref[8] = above_left;
        for (int i = 0; i < 8; i++) ref[9 + i] = above[i];

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = 8 + c - r;
                idx = std::clamp(idx, 0, 16);
                pred[r * 8 + c] = ref[idx];
            }
        }
        break;
    }
    case PredMode::D117: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = c - (r >> 1);
                if (idx >= 0 && idx < 8) {
                    if (r & 1) {
                        pred[r * 8 + c] = clamp_px(
                            (above[std::max(0, idx - 1)] + above[idx] + 1) / 2,
                            max_val);
                    } else {
                        pred[r * 8 + c] = above[idx];
                    }
                } else if (idx < 0) {
                    int li = r + ((-idx - 1) << 1);
                    pred[r * 8 + c] = (li >= 0 && li < 8) ? left[li] : left[7];
                } else {
                    pred[r * 8 + c] = above[7];
                }
            }
        }
        break;
    }
    case PredMode::D153: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = r - (c >> 1);
                if (idx >= 0 && idx < 8) {
                    if (c & 1) {
                        pred[r * 8 + c] = clamp_px(
                            (left[std::max(0, idx - 1)] + left[idx] + 1) / 2,
                            max_val);
                    } else {
                        pred[r * 8 + c] = left[idx];
                    }
                } else if (idx < 0) {
                    int ai = c + ((-idx - 1) << 1);
                    pred[r * 8 + c] = (ai >= 0 && ai < 8) ? above[ai] : above[7];
                } else {
                    pred[r * 8 + c] = left[7];
                }
            }
        }
        break;
    }
    case PredMode::D207: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = r + (c >> 1);
                if (idx < 8) {
                    if (c & 1) {
                        int next = std::min(idx + 1, 7);
                        pred[r * 8 + c] = clamp_px(
                            (left[idx] + left[next] + 1) / 2, max_val);
                    } else {
                        pred[r * 8 + c] = left[idx];
                    }
                } else {
                    pred[r * 8 + c] = left[7];
                }
            }
        }
        break;
    }
    case PredMode::D63: {

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int idx = c + (r >> 1);
                if (idx < 8) {
                    if (r & 1) {
                        int next = std::min(idx + 1, 7);
                        pred[r * 8 + c] = clamp_px(
                            (above[idx] + above[next] + 1) / 2, max_val);
                    } else {
                        pred[r * 8 + c] = above[idx];
                    }
                } else {
                    pred[r * 8 + c] = above[7];
                }
            }
        }
        break;
    }
    default:
        std::fill(pred, pred + 64, mid);
        break;
    }
}



float compute_ssd_8x8(const int16_t* a, const int16_t* b) {
    float ssd = 0;
    for (int i = 0; i < 64; i++) {
        float d = static_cast<float>(a[i]) - static_cast<float>(b[i]);
        ssd += d * d;
    }
    return ssd;
}



float estimate_bits_8x8(const int16_t* residual) {


    float bits = 0;
    int nonzero = 0;
    for (int i = 0; i < 64; i++) {
        if (residual[i] != 0) {
            bits += std::log2(std::abs(static_cast<float>(residual[i])) + 1.0f) + 1.0f;
            nonzero++;
        }
    }

    bits += 4.0f;

    bits += 1.0f;
    return bits;
}



RdResult select_best_mode(const int16_t* original, const int16_t* above,
                           const int16_t* left, int16_t above_left,
                           float lambda, int16_t max_val) {
    RdResult best{PredMode::DC, std::numeric_limits<float>::max()};

    int16_t pred[64];
    int16_t residual[64];

    for (int m = 0; m < static_cast<int>(PredMode::NUM_MODES); m++) {
        PredMode mode = static_cast<PredMode>(m);


        predict_8x8(mode, above, left, above_left, pred, max_val);


        for (int i = 0; i < 64; i++) {
            residual[i] = original[i] - pred[i];
        }


        float ssd = compute_ssd_8x8(original, pred);
        float bits = estimate_bits_8x8(residual);
        float cost = ssd + lambda * bits;

        if (cost < best.cost) {
            best.mode = mode;
            best.cost = cost;
        }
    }

    return best;
}

}
