#pragma once


#include "common.h"
#include <array>
#include <cstdint>

namespace wk {



enum class PredMode : uint8_t {
    DC       = 0,
    V        = 1,
    H        = 2,
    TM       = 3,
    DC_LEFT  = 4,
    DC_TOP   = 5,
    DC_128   = 6,
    D45      = 7,
    D135     = 8,
    D117     = 9,
    D153     = 10,
    D207     = 11,
    D63      = 12,
    NUM_MODES = 13,
};











void predict_8x8(PredMode mode, const int16_t* above, const int16_t* left,
                  int16_t above_left, int16_t* pred, int16_t max_val);



struct RdResult {
    PredMode mode;
    float    cost;
};










RdResult select_best_mode(const int16_t* original, const int16_t* above,
                           const int16_t* left, int16_t above_left,
                           float lambda, int16_t max_val);


float compute_ssd_8x8(const int16_t* a, const int16_t* b);


float estimate_bits_8x8(const int16_t* residual);

}
