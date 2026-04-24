#pragma once

#include "common.h"

namespace wk {

[[nodiscard]] constexpr size_t packed_coefficient_sign_bytes(size_t sign_count) {
    return (sign_count + 7) / 8;
}

[[nodiscard]] Result<std::vector<uint8_t>> pack_coefficient_signs(std::span<const uint8_t> signs);
[[nodiscard]] Result<std::vector<uint8_t>> unpack_coefficient_signs(std::span<const uint8_t> bytes,
                                                                    size_t expected_count,
                                                                    std::string_view label);
[[nodiscard]] Result<void> write_packed_coefficient_signs(ByteWriter& writer,
                                                          std::span<const uint8_t> signs);
[[nodiscard]] Result<std::vector<uint8_t>> read_packed_coefficient_signs(ByteReader& reader,
                                                                         size_t expected_count,
                                                                         std::string_view label);

}
