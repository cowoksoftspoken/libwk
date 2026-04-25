#include <gtest/gtest.h>

#include <array>
#include <utility>

#include "../src/lossy_coeff_stream.h"

using namespace wk;

namespace {

[[nodiscard]] DctBlockI16 make_block(std::initializer_list<std::pair<int, int>> coeffs) {
    DctBlockI16 block{};
    for (const auto& [index, value] : coeffs) {
        block[static_cast<size_t>(index)] = static_cast<int16_t>(value);
    }
    return block;
}

void expect_blocks_equal(std::span<const DctBlockI16> actual,
                         std::span<const DctBlockI16> expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t block_index = 0; block_index < expected.size(); ++block_index) {
        EXPECT_EQ(actual[block_index], expected[block_index]) << "block " << block_index;
    }
}

}

TEST(LossyCoeffStreamTest, PlanePayloadRoundTripsAcrossTableModes) {
    const std::array<DctBlockI16, 3> blocks = {
        make_block({{0, 12}, {1, -4}, {3, 2}}),
        make_block({{0, -12}, {1, 4}, {2, 1}}),
        make_block({{0, 6}})
    };
    const std::array<uint8_t, 3> spans = {4, 3, 1};

    for (const LossyCoeffStreamConfig config : {
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = false,
                                    .split_magnitude_signs = false},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = false},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = true,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = true},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = false,
                                    .use_table_bank = true},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = true,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = true,
                                    .use_table_bank = true},
         }) {
        auto payload = encode_lossy_plane_payload(blocks, spans, 4, config);
        ASSERT_TRUE(payload.has_value()) << payload.error().message;

        ByteReader reader(*payload);
        auto decoded = decode_lossy_plane_payload(reader, spans, 4, config);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
        expect_blocks_equal(*decoded, blocks);
        EXPECT_EQ(reader.remaining(), 0u);
    }
}

TEST(LossyCoeffStreamTest, SharedChromaPayloadRoundTripsAcrossTableModes) {
    const std::array<DctBlockI16, 3> cb_blocks = {
        make_block({{0, 3}, {1, -2}, {2, 1}}),
        make_block({{0, -3}, {1, 2}, {3, -1}}),
        make_block({{0, 1}})
    };
    const std::array<DctBlockI16, 3> cr_blocks = {
        make_block({{0, -5}, {1, 2}, {2, -1}}),
        make_block({{0, 5}, {1, -2}, {3, 1}}),
        make_block({{0, -1}})
    };
    const std::array<uint8_t, 3> spans = {4, 4, 1};

    for (const LossyCoeffStreamConfig config : {
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = false,
                                    .split_magnitude_signs = false},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = false},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = true,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = true},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = false,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = false,
                                    .use_table_bank = true},
             LossyCoeffStreamConfig{.use_plane_max_coeff_span = true,
                                    .adaptive_coefficient_tables = true,
                                    .split_magnitude_signs = true,
                                    .use_table_bank = true},
         }) {
        auto payload = encode_lossy_chroma_payload(cb_blocks, cr_blocks, spans, 4, config);
        ASSERT_TRUE(payload.has_value()) << payload.error().message;

        ByteReader reader(*payload);
        auto decoded = decode_lossy_chroma_payload(reader, spans, 4, config);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
        expect_blocks_equal(decoded->cb_blocks, cb_blocks);
        expect_blocks_equal(decoded->cr_blocks, cr_blocks);
        EXPECT_EQ(reader.remaining(), 0u);
    }
}

TEST(LossyCoeffStreamTest, SharedChromaPayloadShrinksWhenPlanesShareStatistics) {
    const std::array<DctBlockI16, 8> cb_blocks = {
        make_block({{0, 4}, {1, 1}, {2, -1}}),
        make_block({{0, 4}, {1, 0}, {2, 1}}),
        make_block({{0, -4}, {1, -1}, {2, 0}}),
        make_block({{0, -4}, {1, 1}, {2, 0}}),
        make_block({{0, 2}, {1, 0}, {2, 1}}),
        make_block({{0, 2}, {1, -1}, {2, -1}}),
        make_block({{0, -2}, {1, 1}, {2, 1}}),
        make_block({{0, -2}, {1, 0}, {2, -1}}),
    };
    const std::array<DctBlockI16, 8> cr_blocks = {
        make_block({{0, -4}, {1, -1}, {2, 0}}),
        make_block({{0, 2}, {1, 0}, {2, 1}}),
        make_block({{0, -2}, {1, 1}, {2, 1}}),
        make_block({{0, 4}, {1, 1}, {2, -1}}),
        make_block({{0, -4}, {1, 1}, {2, 0}}),
        make_block({{0, 2}, {1, -1}, {2, -1}}),
        make_block({{0, -2}, {1, 0}, {2, -1}}),
        make_block({{0, 4}, {1, 0}, {2, 1}}),
    };
    const std::array<uint8_t, 8> spans = {3, 3, 3, 3, 3, 3, 3, 3};
    const LossyCoeffStreamConfig config{
        .use_plane_max_coeff_span = false,
        .adaptive_coefficient_tables = true,
        .split_magnitude_signs = false,
    };

    auto independent_cb = encode_lossy_plane_payload(cb_blocks, spans, 3, config);
    ASSERT_TRUE(independent_cb.has_value()) << independent_cb.error().message;
    auto independent_cr = encode_lossy_plane_payload(cr_blocks, spans, 3, config);
    ASSERT_TRUE(independent_cr.has_value()) << independent_cr.error().message;
    auto shared = encode_lossy_chroma_payload(cb_blocks, cr_blocks, spans, 3, config);
    ASSERT_TRUE(shared.has_value()) << shared.error().message;

    EXPECT_LT(shared->size(), independent_cb->size() + independent_cr->size());
}

TEST(LossyCoeffStreamTest, SingleSymbolStreamElisionShrinksSplitMagnitudePayload) {
    const std::array<DctBlockI16, 5> blocks = {
        make_block({{0, 3}, {1, -2}}),
        make_block({{0, -3}, {1, 2}}),
        make_block({{0, 3}, {1, -2}}),
        make_block({{0, -3}, {1, 2}}),
        make_block({{0, 3}, {1, -2}})
    };
    const std::array<uint8_t, 5> spans = {2, 2, 2, 2, 2};

    const LossyCoeffStreamConfig base_config{
        .use_plane_max_coeff_span = false,
        .adaptive_coefficient_tables = true,
        .split_magnitude_signs = true,
    };
    const LossyCoeffStreamConfig elided_config{
        .use_plane_max_coeff_span = false,
        .adaptive_coefficient_tables = true,
        .split_magnitude_signs = true,
        .elide_single_symbol_streams = true,
    };

    auto base_payload = encode_lossy_plane_payload(blocks, spans, 2, base_config);
    ASSERT_TRUE(base_payload.has_value()) << base_payload.error().message;
    auto elided_payload = encode_lossy_plane_payload(blocks, spans, 2, elided_config);
    ASSERT_TRUE(elided_payload.has_value()) << elided_payload.error().message;

    EXPECT_LT(elided_payload->size(), base_payload->size());

    ByteReader reader(*elided_payload);
    auto decoded = decode_lossy_plane_payload(reader, spans, 2, elided_config);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    expect_blocks_equal(*decoded, blocks);
}
