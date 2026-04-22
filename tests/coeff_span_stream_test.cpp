#include <gtest/gtest.h>

#include "../src/coeff_span_stream.h"
#include "../src/common.h"

using namespace wk;

TEST(CoeffSpanStreamTest, PackAndUnpackSpansRoundTrip) {
    const std::array<uint8_t, 6> spans = {0, 1, 7, 8, 31, 64};

    auto packed = pack_coefficient_spans(spans);
    ASSERT_TRUE(packed.has_value()) << packed.error().message;
    EXPECT_EQ(packed->size(), packed_coefficient_span_bytes(spans.size()));

    auto unpacked = unpack_coefficient_spans(*packed, spans.size(), "luma");
    ASSERT_TRUE(unpacked.has_value()) << unpacked.error().message;
    EXPECT_EQ(unpacked->size(), spans.size());
    EXPECT_TRUE(std::equal(unpacked->begin(), unpacked->end(), spans.begin()));
}

TEST(CoeffSpanStreamTest, RejectsInvalidSpanValue) {
    const std::array<uint8_t, 1> packed = {0x41};
    auto unpacked = unpack_coefficient_spans(packed, 1, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}

TEST(CoeffSpanStreamTest, RejectsNonZeroPaddingBits) {
    const std::array<uint8_t, 1> packed = {0x80};
    auto unpacked = unpack_coefficient_spans(packed, 1, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}

TEST(CoeffSpanStreamTest, ReaderRejectsUnexpectedPackedLength) {
    ByteWriter writer;
    writer.write_u16(1);
    writer.write_u8(0x01);
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto unpacked = read_packed_coefficient_spans(reader, 2, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}
