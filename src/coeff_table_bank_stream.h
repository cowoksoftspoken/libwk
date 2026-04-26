#pragma once

#include "coeff_table_stream.h"

namespace wk {

enum class CoeffTableBankMode : uint8_t {
    InlineTables = 0,
    SingleTable = 1,
    PackedNibbleIndices = 2,
    RawByteIndices = 3,
    PackedBitIndices = 4,
    PackedTwoBitIndices = 5,
};

[[nodiscard]] Result<void> write_coefficient_table_bank(ByteWriter& writer,
                                                        std::span<const LossyCoeffTable> tables,
                                                        int num_symbols);
[[nodiscard]] Result<std::vector<LossyCoeffTable>> read_coefficient_table_bank(ByteReader& reader,
                                                                                size_t expected_count,
                                                                                int num_symbols,
                                                                                std::string_view label);

}
