#include <gtest/gtest.h>

#include "../src/coeff_sign_stream.h"

using namespace wk;

TEST(CoeffSignStreamTest, PackAndUnpackSignsRoundTrip) {
    const std::array<uint8_t, 10> signs = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0};

    auto packed = pack_coefficient_signs(signs);
    ASSERT_TRUE(packed.has_value()) << packed.error().message;
    EXPECT_EQ(packed->size(), packed_coefficient_sign_bytes(signs.size()));

    auto unpacked = unpack_coefficient_signs(*packed, signs.size(), "lossy");
    ASSERT_TRUE(unpacked.has_value()) << unpacked.error().message;
    EXPECT_TRUE(std::equal(unpacked->begin(), unpacked->end(), signs.begin()));
}

TEST(CoeffSignStreamTest, RejectsInvalidSignValue) {
    const std::array<uint8_t, 1> signs = {2};
    auto packed = pack_coefficient_signs(signs);
    EXPECT_FALSE(packed.has_value());
    EXPECT_EQ(packed.error().code, ErrorCode::InvalidParameter);
}

TEST(CoeffSignStreamTest, RejectsUnexpectedPackedLength) {
    const std::array<uint8_t, 1> packed = {0x01};
    auto unpacked = unpack_coefficient_signs(packed, 9, "lossy");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}

TEST(CoeffSignStreamTest, RejectsNonZeroPaddingBits) {
    const std::array<uint8_t, 2> packed = {0x01, 0x80};
    auto unpacked = unpack_coefficient_signs(packed, 9, "lossy");
    EXPECT_FALSE(unpacked.has_value());
    EXPECT_EQ(unpacked.error().code, ErrorCode::DecodeFailed);
}
