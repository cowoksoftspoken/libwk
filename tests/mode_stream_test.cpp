#include <gtest/gtest.h>

#include "../src/common.h"
#include "../src/mode_stream.h"

using namespace wk;

TEST(ModeStreamTest, PackAndUnpackModesRoundTrip) {
    const std::vector<PredMode> modes = {
        PredMode::DC,
        PredMode::TM,
        PredMode::D45,
        PredMode::D117,
        PredMode::H
    };

    auto packed = pack_prediction_modes(modes);
    ASSERT_TRUE(packed.has_value()) << packed.error().message;
    EXPECT_EQ(packed->size(), packed_prediction_mode_bytes(modes.size()));

    auto unpacked = unpack_prediction_modes(*packed, modes.size(), "luma");
    ASSERT_TRUE(unpacked.has_value()) << unpacked.error().message;
    EXPECT_EQ(*unpacked, modes);
}

TEST(ModeStreamTest, RejectsInvalidModeNibble) {
    const std::array<uint8_t, 1> packed = {0x0F};
    auto unpacked = unpack_prediction_modes(packed, 1, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::PredictionError);
}

TEST(ModeStreamTest, RejectsNonZeroPaddingNibble) {
    const std::array<uint8_t, 1> packed = {0x10};
    auto unpacked = unpack_prediction_modes(packed, 1, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::PredictionError);
}

TEST(ModeStreamTest, ReaderRejectsUnexpectedPackedLength) {
    ByteWriter writer;
    writer.write_u16(1);
    writer.write_u8(0x21);
    const auto bytes = writer.finish();

    ByteReader reader(bytes);
    auto unpacked = read_packed_prediction_modes(reader, 3, "luma");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::PredictionError);
}
