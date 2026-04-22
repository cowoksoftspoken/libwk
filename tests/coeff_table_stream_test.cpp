#include <gtest/gtest.h>

#include "../src/coeff_table_stream.h"

using namespace wk;

namespace {

LossyCoeffTable make_table(const std::initializer_list<std::pair<int, uint32_t>>& entries,
                           int num_symbols) {
    std::vector<uint32_t> counts(static_cast<size_t>(num_symbols), 0);
    for (const auto& [symbol, count] : entries) {
        counts[static_cast<size_t>(symbol)] = count;
    }

    LossyCoeffTable table;
    table.build_from_counts(counts.data(), num_symbols);
    return table;
}

}

TEST(CoeffTableStreamTest, SingleSymbolRoundTrip) {
    constexpr int kNumSymbols = 8;
    auto table = make_table({{3, 1}}, kNumSymbols);

    ByteWriter writer;
    auto written = write_coefficient_table(writer, table, kNumSymbols);
    ASSERT_TRUE(written.has_value()) << written.error().message;

    const auto bytes = writer.finish();
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.front(), static_cast<uint8_t>(CoeffTableEncoding::SingleSymbol));

    ByteReader reader(bytes);
    auto parsed = read_coefficient_table(reader, kNumSymbols, "unit");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->symbol(3).freq, LossyCoeffTable::TABLE_SIZE);
}

TEST(CoeffTableStreamTest, SparsePairsRoundTrip) {
    constexpr int kNumSymbols = 32;
    auto table = make_table({{1, 7}, {17, 5}, {31, 2}}, kNumSymbols);

    ByteWriter writer;
    auto written = write_coefficient_table(writer, table, kNumSymbols);
    ASSERT_TRUE(written.has_value()) << written.error().message;

    const auto bytes = writer.finish();
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.front(), static_cast<uint8_t>(CoeffTableEncoding::SparsePairs));

    ByteReader reader(bytes);
    auto parsed = read_coefficient_table(reader, kNumSymbols, "unit");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    for (int i = 0; i < kNumSymbols; ++i) {
        EXPECT_EQ(parsed->symbol(i).freq, table.symbol(i).freq);
    }
}

TEST(CoeffTableStreamTest, DenseRangeRoundTrip) {
    constexpr int kNumSymbols = 32;
    auto table = make_table({{8, 3}, {9, 5}, {10, 9}, {11, 4}}, kNumSymbols);

    ByteWriter writer;
    auto written = write_coefficient_table(writer, table, kNumSymbols);
    ASSERT_TRUE(written.has_value()) << written.error().message;

    const auto bytes = writer.finish();
    ASSERT_FALSE(bytes.empty());
    EXPECT_EQ(bytes.front(), static_cast<uint8_t>(CoeffTableEncoding::DenseRange));

    ByteReader reader(bytes);
    auto parsed = read_coefficient_table(reader, kNumSymbols, "unit");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    for (int i = 0; i < kNumSymbols; ++i) {
        EXPECT_EQ(parsed->symbol(i).freq, table.symbol(i).freq);
    }
}

TEST(CoeffTableStreamTest, RejectsUnknownEncoding) {
    ByteWriter writer;
    writer.write_u8(0xFF);
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto parsed = read_coefficient_table(reader, 8, "unit");
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, ErrorCode::RansError);
}
