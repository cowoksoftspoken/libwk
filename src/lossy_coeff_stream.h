#pragma once

#include "common.h"
#include "dct.h"

namespace wk {

struct LossyCoeffStreamConfig {
    bool use_plane_max_coeff_span = false;
    bool adaptive_coefficient_tables = true;
    bool split_magnitude_signs = false;
    bool use_table_bank = false;
    bool elide_single_symbol_streams = false;
    bool use_significance_maps = false;
};

struct DecodedLossyChromaPayload {
    std::vector<DctBlockI16> cb_blocks;
    std::vector<DctBlockI16> cr_blocks;
};

[[nodiscard]] Result<std::vector<uint8_t>> encode_lossy_plane_payload(
    std::span<const DctBlockI16> blocks,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config);

[[nodiscard]] Result<std::vector<uint8_t>> encode_lossy_chroma_payload(
    std::span<const DctBlockI16> cb_blocks,
    std::span<const DctBlockI16> cr_blocks,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config);

[[nodiscard]] Result<std::vector<DctBlockI16>> decode_lossy_plane_payload(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config);

[[nodiscard]] Result<DecodedLossyChromaPayload> decode_lossy_chroma_payload(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config);

}
