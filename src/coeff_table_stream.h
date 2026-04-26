#pragma once

#include "common.h"
#include "rans.h"

namespace wk {

enum class CoeffTableEncoding : uint8_t {
    SingleSymbol = 0,
    DenseRange = 1,
    SparsePairs = 2,
    DenseRangeU8 = 3,
    SparsePairsU8 = 4,
};

using LossyCoeffTable = RansTable<RANS_PRECISION_BITS>;

[[nodiscard]] Result<void> write_coefficient_table(ByteWriter& writer,
                                                   const LossyCoeffTable& table,
                                                   int num_symbols);
[[nodiscard]] Result<LossyCoeffTable> read_coefficient_table(ByteReader& reader,
                                                             int num_symbols,
                                                             std::string_view label);

}
