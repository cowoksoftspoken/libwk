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

#include <wk/wk.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

extern "C" {

EMSCRIPTEN_KEEPALIVE
void* wk_wasm_alloc(size_t size) {
    return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void wk_wasm_free(void* ptr) {
    free(ptr);
}

EMSCRIPTEN_KEEPALIVE
int wk_wasm_decode(
    const uint8_t* data, size_t size,
    uint32_t* out_width, uint32_t* out_height,
    uint32_t* out_bpp, uint32_t* out_pixels_ptr
) {
    uint8_t* out_pixels = nullptr;
    uint32_t w = 0, h = 0;
    uint8_t bpp = 0;

    wk_error_t err = wk_decode(data, size, &out_pixels, &w, &h, &bpp);

    *out_width = w;
    *out_height = h;
    *out_bpp = bpp;
    *out_pixels_ptr = (uint32_t)(uintptr_t)out_pixels;
    return (int)err;
}

EMSCRIPTEN_KEEPALIVE
int wk_wasm_encode(
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint8_t bpp, float quality, uint8_t lossless,
    uint32_t* out_size, uint32_t* out_data_ptr
) {
    wk_encoder_config_t config;
    wk_encoder_config_init(&config);
    config.quality = quality;
    config.lossless = lossless;
    config.threads = 1;

    uint8_t* out_data = nullptr;
    size_t sz = 0;
    wk_error_t err = wk_encode(
        pixels, width, height, width * bpp, bpp,
        &config, &out_data, &sz
    );

    *out_size = (uint32_t)sz;
    *out_data_ptr = (uint32_t)(uintptr_t)out_data;
    return (int)err;
}

EMSCRIPTEN_KEEPALIVE
const char* wk_wasm_version(void) {
    return wk_version();
}

}
