#pragma once

#include "common.h"

namespace wk {

enum class CoeffPresenceEncoding : uint8_t {
    AllZero = 0,
    AllOne = 1,
    RawPacked = 2,
};

[[nodiscard]] constexpr size_t packed_coefficient_presence_bytes(size_t count) {
    return (count + 7) / 8;
}

[[nodiscard]] Result<std::vector<uint8_t>> pack_coefficient_presence(std::span<const uint8_t> presence);
[[nodiscard]] Result<std::vector<uint8_t>> unpack_coefficient_presence(std::span<const uint8_t> bytes,
                                                                       size_t expected_count,
                                                                       std::string_view label);
[[nodiscard]] Result<void> write_adaptive_coefficient_presence(ByteWriter& writer,
                                                               std::span<const uint8_t> presence);
[[nodiscard]] Result<std::vector<uint8_t>> read_adaptive_coefficient_presence(ByteReader& reader,
                                                                              size_t expected_count,
                                                                              std::string_view label);

}
