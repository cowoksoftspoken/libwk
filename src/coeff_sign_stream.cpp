#include "coeff_sign_stream.h"

namespace wk {

namespace {

[[nodiscard]] Error invalid_sign_error(std::string_view label, uint8_t sign_value) {
    return Error{ErrorCode::DecodeFailed,
                 std::string("invalid ") + std::string(label) +
                     " coefficient sign value " + std::to_string(sign_value)};
}

}

Result<std::vector<uint8_t>> pack_coefficient_signs(std::span<const uint8_t> signs) {
    const size_t packed_size = packed_coefficient_sign_bytes(signs.size());
    std::vector<uint8_t> packed(packed_size, 0);

    for (size_t i = 0; i < signs.size(); ++i) {
        if (signs[i] > 1) {
            return std::unexpected(Error{ErrorCode::InvalidParameter,
                                         "coefficient sign value is out of range"});
        }
        packed[i / 8] |= static_cast<uint8_t>(signs[i] << (i % 8));
    }

    return packed;
}

Result<std::vector<uint8_t>> unpack_coefficient_signs(std::span<const uint8_t> bytes,
                                                      size_t expected_count,
                                                      std::string_view label) {
    const size_t expected_bytes = packed_coefficient_sign_bytes(expected_count);
    if (bytes.size() != expected_bytes) {
        return std::unexpected(
            Error{ErrorCode::DecodeFailed,
                  std::string(label) + " coefficient sign stream size mismatch"});
    }

    std::vector<uint8_t> signs(expected_count, 0);
    for (size_t i = 0; i < expected_count; ++i) {
        const uint8_t sign_value = static_cast<uint8_t>((bytes[i / 8] >> (i % 8)) & 1u);
        if (sign_value > 1) {
            return std::unexpected(invalid_sign_error(label, sign_value));
        }
        signs[i] = sign_value;
    }

    const size_t used_bits_in_last_byte = expected_count % 8;
    if (!bytes.empty() && used_bits_in_last_byte != 0) {
        const uint8_t valid_mask = static_cast<uint8_t>((1u << used_bits_in_last_byte) - 1u);
        if ((bytes.back() & static_cast<uint8_t>(~valid_mask)) != 0) {
            return std::unexpected(
                Error{ErrorCode::DecodeFailed,
                      std::string(label) + " coefficient sign stream has non-zero padding"});
        }
    }

    return signs;
}

Result<void> write_packed_coefficient_signs(ByteWriter& writer, std::span<const uint8_t> signs) {
    auto packed = pack_coefficient_signs(signs);
    if (!packed) {
        return std::unexpected(packed.error());
    }
    writer.write_bytes(*packed);
    return {};
}

Result<std::vector<uint8_t>> read_packed_coefficient_signs(ByteReader& reader,
                                                           size_t expected_count,
                                                           std::string_view label) {
    const size_t packed_size = packed_coefficient_sign_bytes(expected_count);
    auto packed_bytes = reader.read_bytes(packed_size);
    if (!packed_bytes) {
        return std::unexpected(packed_bytes.error());
    }
    return unpack_coefficient_signs(*packed_bytes, expected_count, label);
}

}
