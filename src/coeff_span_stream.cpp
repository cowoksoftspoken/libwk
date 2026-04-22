#include "coeff_span_stream.h"

#include <limits>

namespace wk {

namespace {

[[nodiscard]] Error invalid_span_error(std::string_view label, uint8_t span_value) {
    return Error{ErrorCode::DecodeFailed,
                 std::string("invalid ") + std::string(label) +
                     " coefficient span value " + std::to_string(span_value)};
}

}

uint8_t compute_coefficient_span(const DctBlockI16& block) {
    for (int i = 63; i >= 0; --i) {
        if (block[i] != 0) {
            return static_cast<uint8_t>(i + 1);
        }
    }
    return 0;
}

Result<std::vector<uint8_t>> pack_coefficient_spans(std::span<const uint8_t> spans) {
    const size_t packed_size = packed_coefficient_span_bytes(spans.size());
    if (packed_size > std::numeric_limits<uint16_t>::max()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "packed coefficient span stream is too large"});
    }

    std::vector<uint8_t> packed(packed_size, 0);
    size_t bit_offset = 0;
    for (uint8_t span_value : spans) {
        if (span_value > 64) {
            return std::unexpected(Error{ErrorCode::InvalidParameter,
                                         "coefficient span value is out of range"});
        }

        const size_t byte_index = bit_offset / 8;
        const size_t shift = bit_offset % 8;
        const uint16_t bits = static_cast<uint16_t>(span_value) << shift;
        packed[byte_index] |= static_cast<uint8_t>(bits & 0xFFu);
        if (byte_index + 1 < packed.size()) {
            packed[byte_index + 1] |= static_cast<uint8_t>(bits >> 8);
        }
        bit_offset += 7;
    }

    return packed;
}

Result<std::vector<uint8_t>> unpack_coefficient_spans(std::span<const uint8_t> bytes,
                                                      size_t expected_count,
                                                      std::string_view label) {
    const size_t expected_bytes = packed_coefficient_span_bytes(expected_count);
    if (bytes.size() != expected_bytes) {
        return std::unexpected(
            Error{ErrorCode::DecodeFailed,
                  std::string(label) + " coefficient span stream size mismatch"});
    }

    std::vector<uint8_t> spans(expected_count, 0);
    size_t bit_offset = 0;
    for (size_t i = 0; i < expected_count; ++i) {
        const size_t byte_index = bit_offset / 8;
        const size_t shift = bit_offset % 8;
        uint16_t bits = bytes[byte_index];
        if (byte_index + 1 < bytes.size()) {
            bits |= static_cast<uint16_t>(bytes[byte_index + 1]) << 8;
        }

        const uint8_t span_value = static_cast<uint8_t>((bits >> shift) & 0x7Fu);
        if (span_value > 64) {
            return std::unexpected(invalid_span_error(label, span_value));
        }
        spans[i] = span_value;
        bit_offset += 7;
    }

    const size_t used_bits_in_last_byte = (expected_count * 7) % 8;
    if (!bytes.empty() && used_bits_in_last_byte != 0) {
        const uint8_t valid_mask = static_cast<uint8_t>((1u << used_bits_in_last_byte) - 1u);
        if ((bytes.back() & static_cast<uint8_t>(~valid_mask)) != 0) {
            return std::unexpected(
                Error{ErrorCode::DecodeFailed,
                      std::string(label) + " coefficient span stream has non-zero padding"});
        }
    }

    return spans;
}

Result<void> write_packed_coefficient_spans(ByteWriter& writer, std::span<const uint8_t> spans) {
    auto packed = pack_coefficient_spans(spans);
    if (!packed) {
        return std::unexpected(packed.error());
    }

    writer.write_u16(static_cast<uint16_t>(packed->size()));
    writer.write_bytes(*packed);
    return {};
}

Result<std::vector<uint8_t>> read_packed_coefficient_spans(ByteReader& reader,
                                                           size_t expected_count,
                                                           std::string_view label) {
    auto packed_size = reader.read_u16();
    if (!packed_size) {
        return std::unexpected(packed_size.error());
    }

    const size_t expected_bytes = packed_coefficient_span_bytes(expected_count);
    if (*packed_size != expected_bytes) {
        return std::unexpected(
            Error{ErrorCode::DecodeFailed,
                  std::string(label) + " coefficient span stream size mismatch"});
    }

    auto packed_bytes = reader.read_bytes(*packed_size);
    if (!packed_bytes) {
        return std::unexpected(packed_bytes.error());
    }

    return unpack_coefficient_spans(*packed_bytes, expected_count, label);
}

}
