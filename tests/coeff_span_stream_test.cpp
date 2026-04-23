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

TEST(CoeffSpanStreamTest, AdaptiveWriterUsesSingleValueEncoding) {
    const std::array<uint8_t, 6> spans = {12, 12, 12, 12, 12, 12};

    auto encoded_size = adaptive_coefficient_span_stream_bytes(spans);
    ASSERT_TRUE(encoded_size.has_value()) << encoded_size.error().message;
    EXPECT_EQ(*encoded_size, sizeof(uint16_t));

    ByteWriter writer;
    auto write_result = write_adaptive_coefficient_spans(writer, spans);
    ASSERT_TRUE(write_result.has_value()) << write_result.error().message;
    const auto bytes = writer.finish();
    ASSERT_EQ(bytes.size(), sizeof(uint16_t));

    ByteReader reader(bytes);
    auto unpacked = read_adaptive_coefficient_spans(reader, spans.size(), "luma");
    ASSERT_TRUE(unpacked.has_value()) << unpacked.error().message;
    EXPECT_TRUE(std::equal(unpacked->begin(), unpacked->end(), spans.begin()));
}

TEST(CoeffSpanStreamTest, AdaptiveWriterUsesRunLengthEncodingWhenSmaller) {
    const std::array<uint8_t, 12> spans = {4, 4, 4, 4, 9, 9, 2, 2, 2, 2, 2, 2};

    auto encoded_size = adaptive_coefficient_span_stream_bytes(spans);
    ASSERT_TRUE(encoded_size.has_value()) << encoded_size.error().message;
    EXPECT_LT(*encoded_size, sizeof(uint16_t) + packed_coefficient_span_bytes(spans.size()));

    ByteWriter writer;
    auto write_result = write_adaptive_coefficient_spans(writer, spans);
    ASSERT_TRUE(write_result.has_value()) << write_result.error().message;
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto unpacked = read_adaptive_coefficient_spans(reader, spans.size(), "luma");
    ASSERT_TRUE(unpacked.has_value()) << unpacked.error().message;
    EXPECT_TRUE(std::equal(unpacked->begin(), unpacked->end(), spans.begin()));
}

TEST(CoeffSpanStreamTest, AdaptiveReaderRejectsReservedEncoding) {
    ByteWriter writer;
    writer.write_u16(0xC000);
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto unpacked = read_adaptive_coefficient_spans(reader, 1, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}

TEST(CoeffSpanStreamTest, AdaptiveReaderRejectsInvalidRunLengthPayload) {
    ByteWriter writer;
    writer.write_u16(0x8003);
    writer.write_u16(0);
    writer.write_u8(12);
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto unpacked = read_adaptive_coefficient_spans(reader, 2, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}
