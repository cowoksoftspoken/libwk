#pragma once

#include "common.h"
#include "dct.h"

namespace wk {

[[nodiscard]] constexpr size_t packed_coefficient_span_bytes(size_t span_count) {
    return (span_count * 7 + 7) / 8;
}

[[nodiscard]] uint8_t compute_coefficient_span(const DctBlockI16& block);
[[nodiscard]] Result<std::vector<uint8_t>> pack_coefficient_spans(std::span<const uint8_t> spans);
[[nodiscard]] Result<std::vector<uint8_t>> unpack_coefficient_spans(std::span<const uint8_t> bytes,
                                                                    size_t expected_count,
                                                                    std::string_view label);
[[nodiscard]] Result<void> write_packed_coefficient_spans(ByteWriter& writer,
                                                          std::span<const uint8_t> spans);
[[nodiscard]] Result<std::vector<uint8_t>> read_packed_coefficient_spans(ByteReader& reader,
                                                                         size_t expected_count,
                                                                         std::string_view label);

}
