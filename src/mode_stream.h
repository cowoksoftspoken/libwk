#pragma once

#include "common.h"
#include "predict.h"

namespace wk {

constexpr uint32_t kLossyTileLayoutTagV1 = 0x314d4843u;
constexpr uint32_t kLossyTileLayoutTagV2 = 0x324d4843u;
constexpr uint32_t kLossyTileLayoutTagV3 = 0x334d4843u;
constexpr uint32_t kLossyTileLayoutTagV4 = 0x344d4843u;
constexpr uint32_t kLossyTileLayoutTagV5 = 0x354d4843u;
constexpr uint32_t kLossyTileLayoutTagV6 = 0x364d4843u;

constexpr uint8_t kLossyTileSyntaxFlagAdaptiveSpanStreams = 1u << 0;
constexpr uint8_t kLossyTileSyntaxFlagPlaneCoeffExtents = 1u << 1;
constexpr uint8_t kLossyTileSyntaxFlagSplitMagnitudeSigns = 1u << 2;
constexpr uint8_t kLossyTileSyntaxFlagSharedChromaCoeffTables = 1u << 3;
constexpr uint8_t kLossyTileSyntaxFlagCoefficientTableBank = 1u << 4;
constexpr uint8_t kLossyTileSyntaxFlagElideSingleSymbolStreams = 1u << 5;

[[nodiscard]] constexpr size_t packed_prediction_mode_bytes(size_t mode_count) {
    return (mode_count + 1) / 2;
}

[[nodiscard]] Result<std::vector<uint8_t>> pack_prediction_modes(std::span<const PredMode> modes);
[[nodiscard]] Result<std::vector<PredMode>> unpack_prediction_modes(std::span<const uint8_t> bytes,
                                                                    size_t expected_count,
                                                                    std::string_view label);
[[nodiscard]] Result<void> write_packed_prediction_modes(ByteWriter& writer,
                                                         std::span<const PredMode> modes);
[[nodiscard]] Result<std::vector<PredMode>> read_packed_prediction_modes(ByteReader& reader,
                                                                         size_t expected_count,
                                                                         std::string_view label);

}
