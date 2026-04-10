
#include "common.h"
#include "container.h"
#include "rans.h"
#include "dct.h"
#include "quantize.h"
#include "predict.h"
#include "colorspace.h"
#include "lossless.h"
#include "threading.h"
#include <wk/wk.hpp>
#include <wk/wk.h>
#include <wk/wkmeta.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace wk {



struct TileEncodeResult {
    uint16_t tile_x;
    uint16_t tile_y;
    std::vector<uint8_t> compressed;
    float    quality_score;
};


static float compute_quality_score(const int16_t* original, const int16_t* reconstructed,
                                    uint32_t width, uint32_t height, int16_t max_val) {
    double mse = 0;
    size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; i++) {
        double d = static_cast<double>(original[i]) - reconstructed[i];
        mse += d * d;
    }
    mse /= count;

    if (mse < 1e-10) return 100.0f;

    double psnr = 10.0 * std::log10(
        static_cast<double>(max_val) * max_val / mse);




    float score = static_cast<float>(std::clamp((psnr - 20.0) * 2.5, 0.0, 100.0));
    return score;
}

static std::vector<uint8_t> expand_rgb8_to_rgba8(const Image& image) {
    std::vector<uint8_t> expanded(static_cast<size_t>(image.width()) * image.height() * 4, 255);
    const auto pixels = image.pixels();
    const size_t pixel_count = static_cast<size_t>(image.width()) * image.height();
    for (size_t i = 0; i < pixel_count; ++i) {
        expanded[i * 4 + 0] = pixels[i * 3 + 0];
        expanded[i * 4 + 1] = pixels[i * 3 + 1];
        expanded[i * 4 + 2] = pixels[i * 3 + 2];
    }
    return expanded;
}

static Result<TileEncodeResult> encode_lossy_tile(
    const int16_t* y_plane, const int16_t* cb_plane, const int16_t* cr_plane,
    const int16_t* alpha_plane,
    uint32_t image_width, uint32_t image_height,
    uint32_t tile_x, uint32_t tile_y, uint32_t tile_size,
    float quality, uint8_t bit_depth, bool has_alpha,
    ChromaSubsampling subsampling,
    float target_quality) {

    static_cast<void>(target_quality);

    if (has_alpha && alpha_plane == nullptr) {
        return std::unexpected(Error{ErrorCode::InvalidParameter, "alpha plane missing"});
    }

    int16_t max_val = static_cast<int16_t>((1 << bit_depth) - 1);
    int qp = quality_to_qp(quality);
    float lambda = qp_to_lambda(qp);

    uint32_t x0 = tile_x * tile_size;
    uint32_t y0 = tile_y * tile_size;
    uint32_t tw = std::min(tile_size, image_width - x0);
    uint32_t th = std::min(tile_size, image_height - y0);

    uint32_t blocks_x = (tw + 7) / 8;
    uint32_t blocks_y = (th + 7) / 8;

    QuantTable quant_y, quant_c, quant_a;
    quant_y.build(quality, false, bit_depth);
    quant_c.build(quality, true, bit_depth);
    if (has_alpha) {
        quant_a.build(std::min(100.0f, quality + 10.0f), false, bit_depth);
    }

    struct BlockData {
        PredMode mode;
        DctBlockI16 quantized;
    };

    std::vector<BlockData> y_blocks(blocks_x * blocks_y);
    std::vector<BlockData> cb_blocks, cr_blocks, a_blocks;

    uint32_t chroma_tw = tw;
    uint32_t chroma_th = th;
    uint32_t chroma_blocks_x = blocks_x;
    uint32_t chroma_blocks_y = blocks_y;
    if (subsampling == ChromaSubsampling::YUV420) {
        chroma_tw = (tw + 1) / 2;
        chroma_th = (th + 1) / 2;
        chroma_blocks_x = (chroma_tw + 7) / 8;
        chroma_blocks_y = (chroma_th + 7) / 8;
    }
    cb_blocks.resize(chroma_blocks_x * chroma_blocks_y);
    cr_blocks.resize(chroma_blocks_x * chroma_blocks_y);
    if (has_alpha) {
        a_blocks.resize(blocks_x * blocks_y);
    }

    std::vector<int16_t> tile_y_data(tw * th, 0);
    for (uint32_t r = 0; r < th; ++r) {
        for (uint32_t c = 0; c < tw; ++c) {
            tile_y_data[r * tw + c] = y_plane[(y0 + r) * image_width + (x0 + c)];
        }
    }

    std::vector<int16_t> reconstructed_y(tw * th, 0);

    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            int16_t original_block[64] = {};
            uint32_t bx0 = bx * 8;
            uint32_t by0 = by * 8;

            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    uint32_t px = bx0 + c;
                    uint32_t py = by0 + r;
                    if (px < tw && py < th) {
                        original_block[r * 8 + c] = tile_y_data[py * tw + px];
                    }
                }
            }

            int16_t above[8] = {};
            int16_t left[8] = {};
            int16_t above_left = max_val / 2;
            bool has_above = by > 0;
            bool has_left = bx > 0;

            if (has_above) {
                for (int c = 0; c < 8; ++c) {
                    uint32_t px = bx0 + c;
                    if (px < tw) {
                        above[c] = reconstructed_y[(by0 - 1) * tw + px];
                    }
                }
            }
            if (has_left) {
                for (int r = 0; r < 8; ++r) {
                    uint32_t py = by0 + r;
                    if (py < th) {
                        left[r] = reconstructed_y[py * tw + (bx0 - 1)];
                    }
                }
            }
            if (has_above && has_left) {
                above_left = reconstructed_y[(by0 - 1) * tw + (bx0 - 1)];
            }

            auto rd = select_best_mode(original_block,
                                        has_above ? above : nullptr,
                                        has_left ? left : nullptr,
                                        above_left, lambda, max_val);

            int16_t pred[64];
            predict_8x8(rd.mode, has_above ? above : nullptr,
                        has_left ? left : nullptr, above_left, pred, max_val);

            int16_t residual_block[64];
            for (int i = 0; i < 64; ++i) {
                residual_block[i] = original_block[i] - pred[i];
            }

            DctBlock dct_coeffs;
            for (int i = 0; i < 64; ++i) {
                dct_coeffs[i] = static_cast<float>(residual_block[i]);
            }
            dct_2d_forward(dct_coeffs);

            auto& bd = y_blocks[by * blocks_x + bx];
            bd.mode = rd.mode;
            quant_y.quantize(dct_coeffs, bd.quantized);

            DctBlock recon_dct;
            quant_y.dequantize(bd.quantized, recon_dct);
            dct_2d_inverse(recon_dct);

            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 8; ++c) {
                    uint32_t px = bx0 + c;
                    uint32_t py = by0 + r;
                    if (px < tw && py < th) {
                        int16_t v = static_cast<int16_t>(
                            std::clamp(static_cast<int>(std::round(recon_dct[r * 8 + c]))
                                       + pred[r * 8 + c],
                                       0, static_cast<int>(max_val)));
                        reconstructed_y[py * tw + px] = v;
                    }
                }
            }
        }
    }

    auto encode_chroma_plane = [&](const int16_t* plane, std::vector<BlockData>& blocks,
                                    uint32_t ctw, uint32_t cth, uint32_t cbx, uint32_t cby) {
        std::vector<int16_t> tile_plane(ctw * cth, max_val / 2);

        uint32_t src_w = subsampling == ChromaSubsampling::YUV420 ? (image_width + 1) / 2 : image_width;
        uint32_t src_h = subsampling == ChromaSubsampling::YUV420 ? (image_height + 1) / 2 : image_height;
        uint32_t cx0 = subsampling == ChromaSubsampling::YUV420 ? x0 / 2 : x0;
        uint32_t cy0 = subsampling == ChromaSubsampling::YUV420 ? y0 / 2 : y0;

        for (uint32_t r = 0; r < cth; ++r) {
            for (uint32_t c = 0; c < ctw; ++c) {
                uint32_t sy = cy0 + r;
                uint32_t sx = cx0 + c;
                if (sy < src_h && sx < src_w) {
                    tile_plane[r * ctw + c] = plane[sy * src_w + sx];
                }
            }
        }

        for (uint32_t by = 0; by < cby; ++by) {
            for (uint32_t bx = 0; bx < cbx; ++bx) {
                int16_t block_data[64] = {};
                for (int r = 0; r < 8; ++r) {
                    for (int c = 0; c < 8; ++c) {
                        uint32_t px = bx * 8 + c;
                        uint32_t py = by * 8 + r;
                        if (px < ctw && py < cth) {
                            block_data[r * 8 + c] = tile_plane[py * ctw + px];
                        }
                    }
                }

                auto& bd = blocks[by * cbx + bx];
                bd.mode = PredMode::DC_128;

                DctBlock dct_coeffs;
                for (int i = 0; i < 64; ++i) {
                    dct_coeffs[i] = static_cast<float>(block_data[i] - max_val / 2);
                }
                dct_2d_forward(dct_coeffs);
                quant_c.quantize(dct_coeffs, bd.quantized);
            }
        }
    };

    encode_chroma_plane(cb_plane, cb_blocks, chroma_tw, chroma_th,
                        chroma_blocks_x, chroma_blocks_y);
    encode_chroma_plane(cr_plane, cr_blocks, chroma_tw, chroma_th,
                        chroma_blocks_x, chroma_blocks_y);

    std::vector<int16_t> tile_alpha_data;
    std::vector<int16_t> reconstructed_alpha;
    if (has_alpha) {
        tile_alpha_data.assign(tw * th, max_val);
        reconstructed_alpha.assign(tw * th, max_val);
        for (uint32_t r = 0; r < th; ++r) {
            for (uint32_t c = 0; c < tw; ++c) {
                tile_alpha_data[r * tw + c] = alpha_plane[(y0 + r) * image_width + (x0 + c)];
            }
        }

        for (uint32_t by = 0; by < blocks_y; ++by) {
            for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                int16_t original_block[64] = {};
                uint32_t bx0 = bx * 8;
                uint32_t by0 = by * 8;

                for (int r = 0; r < 8; ++r) {
                    for (int c = 0; c < 8; ++c) {
                        uint32_t px = bx0 + c;
                        uint32_t py = by0 + r;
                        if (px < tw && py < th) {
                            original_block[r * 8 + c] = tile_alpha_data[py * tw + px];
                        }
                    }
                }

                int16_t above[8] = {};
                int16_t left[8] = {};
                int16_t above_left = max_val;
                bool has_above = by > 0;
                bool has_left = bx > 0;

                if (has_above) {
                    for (int c = 0; c < 8; ++c) {
                        uint32_t px = bx0 + c;
                        if (px < tw) {
                            above[c] = reconstructed_alpha[(by0 - 1) * tw + px];
                        }
                    }
                }
                if (has_left) {
                    for (int r = 0; r < 8; ++r) {
                        uint32_t py = by0 + r;
                        if (py < th) {
                            left[r] = reconstructed_alpha[py * tw + (bx0 - 1)];
                        }
                    }
                }
                if (has_above && has_left) {
                    above_left = reconstructed_alpha[(by0 - 1) * tw + (bx0 - 1)];
                }

                auto rd = select_best_mode(original_block,
                                            has_above ? above : nullptr,
                                            has_left ? left : nullptr,
                                            above_left, lambda, max_val);

                int16_t pred[64];
                predict_8x8(rd.mode, has_above ? above : nullptr,
                            has_left ? left : nullptr, above_left, pred, max_val);

                int16_t residual_block[64];
                for (int i = 0; i < 64; ++i) {
                    residual_block[i] = original_block[i] - pred[i];
                }

                DctBlock dct_coeffs;
                for (int i = 0; i < 64; ++i) {
                    dct_coeffs[i] = static_cast<float>(residual_block[i]);
                }
                dct_2d_forward(dct_coeffs);

                auto& bd = a_blocks[by * blocks_x + bx];
                bd.mode = rd.mode;
                quant_a.quantize(dct_coeffs, bd.quantized);

                DctBlock recon_dct;
                quant_a.dequantize(bd.quantized, recon_dct);
                dct_2d_inverse(recon_dct);

                for (int r = 0; r < 8; ++r) {
                    for (int c = 0; c < 8; ++c) {
                        uint32_t px = bx0 + c;
                        uint32_t py = by0 + r;
                        if (px < tw && py < th) {
                            int16_t v = static_cast<int16_t>(
                                std::clamp(static_cast<int>(std::round(recon_dct[r * 8 + c]))
                                           + pred[r * 8 + c],
                                           0, static_cast<int>(max_val)));
                            reconstructed_alpha[py * tw + px] = v;
                        }
                    }
                }
            }
        }
    }

    ByteWriter tile_writer;

    for (int i = 0; i < 64; ++i) tile_writer.write_u16(quant_y.step(i));
    for (int i = 0; i < 64; ++i) tile_writer.write_u16(quant_c.step(i));

    tile_writer.write_u16(static_cast<uint16_t>(blocks_x));
    tile_writer.write_u16(static_cast<uint16_t>(blocks_y));
    tile_writer.write_u16(static_cast<uint16_t>(chroma_blocks_x));
    tile_writer.write_u16(static_cast<uint16_t>(chroma_blocks_y));

    for (const auto& bd : y_blocks) {
        tile_writer.write_u8(static_cast<uint8_t>(bd.mode));
    }

    auto rans_encode_blocks = [&](const std::vector<BlockData>& blocks) {
        constexpr int OFFSET = 1024;
        constexpr int NUM_SYMBOLS = 2049;

        std::vector<std::array<uint32_t, 2049>> freq_counts(64);
        for (auto& fc : freq_counts) fc.fill(0);

        for (const auto& bd : blocks) {
            for (int i = 0; i < 64; ++i) {
                int sym = std::clamp(static_cast<int>(bd.quantized[i]) + OFFSET, 0, NUM_SYMBOLS - 1);
                freq_counts[i][sym]++;
            }
        }

        for (int coeff_idx = 0; coeff_idx < 64; ++coeff_idx) {
            RansTable<RANS_PRECISION_BITS> table;
            table.build_from_counts(freq_counts[coeff_idx].data(), NUM_SYMBOLS);

            int first_nonzero = -1;
            int last_nonzero = -1;
            for (int i = 0; i < NUM_SYMBOLS; ++i) {
                if (table.symbol(i).freq > 0) {
                    if (first_nonzero == -1) first_nonzero = i;
                    last_nonzero = i;
                }
            }
            if (first_nonzero == -1) {
                first_nonzero = OFFSET;
                last_nonzero = OFFSET;
            }

            tile_writer.write_u16(static_cast<uint16_t>(first_nonzero));
            tile_writer.write_u16(static_cast<uint16_t>(last_nonzero));
            for (int i = first_nonzero; i <= last_nonzero; ++i) {
                tile_writer.write_u16(table.symbol(i).freq);
            }

            RansEncoder<RANS_PRECISION_BITS> enc;
            enc.init();
            for (size_t b = blocks.size(); b > 0; --b) {
                int sym = std::clamp(static_cast<int>(blocks[b - 1].quantized[coeff_idx]) + OFFSET,
                                     0, NUM_SYMBOLS - 1);
                enc.encode(table, sym);
            }
            auto encoded = enc.finish();

            tile_writer.write_u32(static_cast<uint32_t>(encoded.size()));
            tile_writer.write_bytes(encoded);
        }
    };

    rans_encode_blocks(y_blocks);
    rans_encode_blocks(cb_blocks);
    rans_encode_blocks(cr_blocks);

    if (has_alpha) {
        for (int i = 0; i < 64; ++i) tile_writer.write_u16(quant_a.step(i));
        for (const auto& bd : a_blocks) {
            tile_writer.write_u8(static_cast<uint8_t>(bd.mode));
        }
        rans_encode_blocks(a_blocks);
    }

    float score = compute_quality_score(tile_y_data.data(), reconstructed_y.data(), tw, th, max_val);

    TileEncodeResult result;
    result.tile_x = static_cast<uint16_t>(tile_x);
    result.tile_y = static_cast<uint16_t>(tile_y);
    result.compressed = tile_writer.finish();
    result.quality_score = score;
    return result;
}

Result<std::vector<uint8_t>> encode(const Image& image, const EncoderConfig& config) {
    uint32_t width = image.width();
    uint32_t height = image.height();

    if (width == 0 || height == 0) {
        return std::unexpected(Error{ErrorCode::InvalidParameter, "zero dimensions"});
    }

    const bool source_has_alpha = image.has_alpha();

    FrameHeader fhdr;
    fhdr.width = width;
    fhdr.height = height;
    fhdr.bit_depth = static_cast<uint8_t>(config.bit_depth);
    fhdr.cicp_primaries = config.cicp.primaries;
    fhdr.cicp_transfer = config.cicp.transfer;
    fhdr.cicp_matrix = config.cicp.matrix;
    fhdr.tile_size_log2 = config.tile_size_log2;

    fhdr.flags = 0;
    if (config.lossless) fhdr.flags |= wk::FHDR_FLAG_LOSSLESS;
    if (source_has_alpha) fhdr.flags |= wk::FHDR_FLAG_ALPHA;
    if (config.bit_depth != BitDepth::Bits8) fhdr.flags |= wk::FHDR_FLAG_HDR;
    if (config.cicp.full_range) fhdr.flags |= wk::FHDR_FLAG_FULL_RANGE;
    fhdr.flags |= FHDR_FLAG_TILED;

    WkFile file;
    file.header = fhdr;

    if (config.lossless) {
        std::vector<uint8_t> expanded_rgb;
        const uint8_t* lossless_pixels = image.pixels().data();
        if (!source_has_alpha && config.bit_depth == BitDepth::Bits8) {
            expanded_rgb = expand_rgb8_to_rgba8(image);
            lossless_pixels = expanded_rgb.data();
        }

        auto encoded = lossless_encode(lossless_pixels, width, height, fhdr.bit_depth);
        if (!encoded) return std::unexpected(encoded.error());

        Chunk tile_chunk;
        std::memcpy(tile_chunk.type, CHUNK_TILE, 4);

        TileHeader th;
        th.tile_x = 0;
        th.tile_y = 0;
        th.layer_flags = TILE_HAS_BASE;
        th.compressed_size = static_cast<uint32_t>(encoded->size());

        ByteWriter tw;
        auto th_data = serialize_tile_header(th);
        tw.write_bytes(th_data);
        tw.write_bytes(*encoded);
        tile_chunk.payload = tw.finish();

        file.tile_chunks.push_back(std::move(tile_chunk));
    } else {
        size_t pixel_count = static_cast<size_t>(width) * height;

        std::vector<int16_t> y_plane(pixel_count);
        std::vector<int16_t> cb_plane_full(pixel_count);
        std::vector<int16_t> cr_plane_full(pixel_count);

        rgb_to_ycbcr(y_plane.data(), cb_plane_full.data(), cr_plane_full.data(),
                      image.pixels().data(), width, height,
                      fhdr.cicp_matrix, fhdr.bit_depth, fhdr.full_range(),
                      source_has_alpha);

        std::vector<int16_t> cb_plane;
        std::vector<int16_t> cr_plane;
        std::vector<int16_t> alpha_plane;
        if (source_has_alpha) {
            alpha_plane.resize(pixel_count);
            if (fhdr.bit_depth == 8) {
                const auto pixels = image.pixels();
                for (size_t i = 0; i < pixel_count; ++i) {
                    alpha_plane[i] = pixels[i * 4 + 3];
                }
            } else {
                const uint16_t* pixels = reinterpret_cast<const uint16_t*>(image.pixels().data());
                for (size_t i = 0; i < pixel_count; ++i) {
                    alpha_plane[i] = static_cast<int16_t>(pixels[i * 4 + 3]);
                }
            }
        }

        if (config.subsampling == Subsampling::YUV420) {
            const uint32_t chroma_w = (width + 1) / 2;
            const uint32_t chroma_h = (height + 1) / 2;
            cb_plane.resize(chroma_w * chroma_h);
            cr_plane.resize(chroma_w * chroma_h);
            subsample_420(cb_plane_full.data(), cb_plane.data(), width, height);
            subsample_420(cr_plane_full.data(), cr_plane.data(), width, height);
        } else {
            cb_plane = std::move(cb_plane_full);
            cr_plane = std::move(cr_plane_full);
        }

        uint32_t tile_size = fhdr.tile_size();
        uint32_t num_tx = fhdr.tiles_x();
        uint32_t num_ty = fhdr.tiles_y();

        ThreadPool pool(config.threads);
        std::vector<std::future<Result<TileEncodeResult>>> futures;

        for (uint32_t ty = 0; ty < num_ty; ty++) {
            for (uint32_t tx = 0; tx < num_tx; tx++) {
                auto future = pool.submit([&, tx, ty]() {
                    return encode_lossy_tile(
                        y_plane.data(), cb_plane.data(), cr_plane.data(),
                        alpha_plane.empty() ? nullptr : alpha_plane.data(),
                        width, height, tx, ty, tile_size,
                        config.quality, fhdr.bit_depth,
                        source_has_alpha,
                        config.subsampling == Subsampling::YUV420 ?
                            ChromaSubsampling::YUV420 : ChromaSubsampling::YUV444,
                        config.target_ssimulacra2);
                });
                futures.push_back(std::move(future));
            }
        }

        float total_score = 0;
        int tile_count = 0;

        for (auto& future : futures) {
            auto result = future.get();
            if (!result) return std::unexpected(result.error());

            Chunk tile_chunk;
            std::memcpy(tile_chunk.type, CHUNK_TILE, 4);

            TileHeader th;
            th.tile_x = result->tile_x;
            th.tile_y = result->tile_y;
            th.layer_flags = static_cast<uint8_t>(TILE_HAS_BASE | (source_has_alpha ? TILE_HAS_ALPHA : 0));
            th.compressed_size = static_cast<uint32_t>(result->compressed.size());

            ByteWriter tw;
            auto th_data = serialize_tile_header(th);
            tw.write_bytes(th_data);
            tw.write_bytes(result->compressed);
            tile_chunk.payload = tw.finish();

            file.tile_chunks.push_back(std::move(tile_chunk));
            total_score += result->quality_score;
            tile_count++;
        }

        if (tile_count > 0) {
            float avg_score = total_score / tile_count;
            meta::MetaBlock meta;
            meta.set(meta::Namespace::Rating, meta::rating::QUALITY_SCORE, avg_score);
            file.metadata = std::move(meta);
            file.header.flags |= wk::FHDR_FLAG_HAS_WKMETA;
        }
    }

    return write_container(file);
}



Image::Image(uint32_t w, uint32_t h, BitDepth bd, bool alpha) {
    info_.width = w;
    info_.height = h;
    info_.bit_depth = bd;
    info_.has_alpha = alpha;

    size_t bpp = bytes_per_pixel();
    pixels_.resize(static_cast<size_t>(w) * h * bpp);
}

uint32_t Image::bytes_per_pixel() const {
    bool is_16 = (info_.bit_depth != BitDepth::Bits8);
    return (info_.has_alpha ? 4u : 3u) * (is_16 ? 2u : 1u);
}

size_t Image::stride() const {
    return static_cast<size_t>(info_.width) * bytes_per_pixel();
}

uint8_t* Image::row(uint32_t y) {
    return pixels_.data() + static_cast<size_t>(y) * stride();
}

const uint8_t* Image::row(uint32_t y) const {
    return pixels_.data() + static_cast<size_t>(y) * stride();
}

}



extern "C" {

void wk_encoder_config_init(wk_encoder_config_t* config) {
    config->quality = 75.0f;
    config->lossless = 0;
    config->bit_depth = 8;
    config->cicp_primaries = 1;
    config->cicp_transfer = 1;
    config->cicp_matrix = 1;
    config->tile_size_log2 = 9;
    config->threads = 0;
    config->target_ssimulacra2 = 0.0f;
    config->chroma_subsampling = 0;
    config->full_range = 1;
}

wk_error_t wk_encode(
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint32_t pixel_stride, uint8_t bpp,
    const wk_encoder_config_t* config,
    uint8_t** out_data, size_t* out_size) {

    if (!pixels || !config || !out_data || !out_size) {
        return WK_ERROR_INVALID_PARAM;
    }

    wk::BitDepth bd = wk::BitDepth::Bits8;
    if (config->bit_depth == 10) bd = wk::BitDepth::Bits10;
    else if (config->bit_depth == 12) bd = wk::BitDepth::Bits12;

    bool has_alpha = (bpp == 4 || bpp == 8);
    wk::Image image(width, height, bd, has_alpha);


    size_t row_bytes = static_cast<size_t>(width) * bpp;
    size_t src_stride = pixel_stride > 0 ? pixel_stride : row_bytes;

    for (uint32_t y = 0; y < height; y++) {
        std::memcpy(image.row(y), pixels + y * src_stride, row_bytes);
    }

    wk::EncoderConfig enc_config;
    enc_config.quality = config->quality;
    enc_config.lossless = config->lossless != 0;
    enc_config.bit_depth = bd;
    enc_config.subsampling = config->chroma_subsampling ?
        wk::Subsampling::YUV420 : wk::Subsampling::YUV444;
    enc_config.cicp = {config->cicp_primaries, config->cicp_transfer,
                       config->cicp_matrix, config->full_range != 0};
    enc_config.tile_size_log2 = config->tile_size_log2;
    enc_config.threads = config->threads;
    enc_config.target_ssimulacra2 = config->target_ssimulacra2;

    auto result = wk::encode(image, enc_config);
    if (!result) {
        return WK_ERROR_ENCODE_FAIL;
    }

    *out_size = result->size();
    *out_data = static_cast<uint8_t*>(std::malloc(result->size()));
    if (!*out_data) return WK_ERROR_OOM;
    std::memcpy(*out_data, result->data(), result->size());

    return WK_OK;
}

wk_error_t wk_get_info(const uint8_t* data, size_t size, wk_image_info_t* info) {
    if (!data || !info) return WK_ERROR_INVALID_PARAM;

    auto result = wk::get_info({data, size});
    if (!result) return WK_ERROR_INVALID_DATA;

    info->width = result->width;
    info->height = result->height;
    info->bit_depth = static_cast<uint8_t>(result->bit_depth);
    info->cicp_primaries = result->cicp.primaries;
    info->cicp_transfer = result->cicp.transfer;
    info->cicp_matrix = result->cicp.matrix;
    info->flags = 0;
    if (result->is_lossless) info->flags |= wk::FHDR_FLAG_LOSSLESS;
    if (result->is_animated) info->flags |= wk::FHDR_FLAG_ANIMATED;
    if (result->has_alpha) info->flags |= wk::FHDR_FLAG_ALPHA;
    if (result->is_hdr) info->flags |= wk::FHDR_FLAG_HDR;
    if (result->has_wkmeta) info->flags |= wk::FHDR_FLAG_HAS_WKMETA;
    if (result->cicp.full_range) info->flags |= wk::FHDR_FLAG_FULL_RANGE;
    info->tile_size_log2 = static_cast<uint8_t>(std::bit_width(result->tile_size) - 1);
    info->max_cll = result->max_cll;
    info->max_fall = result->max_fall;
    info->has_alpha = result->has_alpha;
    info->is_lossless = result->is_lossless;
    info->is_animated = result->is_animated;
    info->is_hdr = result->is_hdr;
    info->frame_count = result->frame_count;

    return WK_OK;
}

void wk_free(void* ptr) {
    std::free(ptr);
}

const char* wk_version(void) {
    return "0.1.0";
}

}



