// Copyright 2026 Inggrit Setya Budi
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "coeff_table_stream.h"

#include <limits>

namespace wk {

namespace {

[[nodiscard]] Error invalid_table_error(std::string_view label, std::string message) {
    return Error{ErrorCode::RansError,
                 std::string(label) + " coefficient table " + std::move(message)};
}

[[nodiscard]] std::vector<int> collect_nonzero_symbols(const LossyCoeffTable& table, int num_symbols) {
    std::vector<int> symbols;
    for (int i = 0; i < num_symbols; ++i) {
        if (table.symbol(i).freq > 0) {
            symbols.push_back(i);
        }
    }
    return symbols;
}

[[nodiscard]] Result<void> validate_exact_counts(const std::vector<uint32_t>& counts,
                                                 std::string_view label) {
    uint64_t total = 0;
    for (uint32_t count : counts) {
        total += count;
    }
    if (total != LossyCoeffTable::TABLE_SIZE) {
        return std::unexpected(
            invalid_table_error(label, "does not sum to rANS table size"));
    }
    return {};
}

[[nodiscard]] bool frequencies_fit_in_u8(const LossyCoeffTable& table,
                                         std::span<const int> symbols) {
    for (int symbol : symbols) {
        if (table.symbol(symbol).freq > std::numeric_limits<uint8_t>::max()) {
            return false;
        }
    }
    return true;
}

}

Result<void> write_coefficient_table(ByteWriter& writer,
                                     const LossyCoeffTable& table,
                                     int num_symbols) {
    if (num_symbols <= 0) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "coefficient table symbol count must be positive"});
    }

    const std::vector<int> nonzero_symbols = collect_nonzero_symbols(table, num_symbols);
    if (nonzero_symbols.empty()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "coefficient table must contain at least one symbol"});
    }

    if (nonzero_symbols.size() == 1 &&
        table.symbol(nonzero_symbols.front()).freq == LossyCoeffTable::TABLE_SIZE) {
        writer.write_u8(static_cast<uint8_t>(CoeffTableEncoding::SingleSymbol));
        writer.write_u16(static_cast<uint16_t>(nonzero_symbols.front()));
        return {};
    }

    const int first_nonzero = nonzero_symbols.front();
    const int last_nonzero = nonzero_symbols.back();
    const size_t dense_symbol_count = static_cast<size_t>(last_nonzero - first_nonzero + 1);
    const bool dense_fits_u8 = [&]() {
        for (int symbol_index = first_nonzero; symbol_index <= last_nonzero; ++symbol_index) {
            if (table.symbol(symbol_index).freq > std::numeric_limits<uint8_t>::max()) {
                return false;
            }
        }
        return true;
    }();
    const bool sparse_fits_u8 = frequencies_fit_in_u8(table, nonzero_symbols);

    if (nonzero_symbols.size() > std::numeric_limits<uint16_t>::max()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "coefficient table has too many sparse symbols"});
    }

    struct Candidate {
        CoeffTableEncoding encoding;
        size_t payload_bytes;
    };

    std::vector<Candidate> candidates;
    candidates.push_back(Candidate{
        .encoding = CoeffTableEncoding::DenseRange,
        .payload_bytes = 4u + dense_symbol_count * sizeof(uint16_t),
    });
    candidates.push_back(Candidate{
        .encoding = CoeffTableEncoding::SparsePairs,
        .payload_bytes = 2u + nonzero_symbols.size() * sizeof(uint16_t) * 2u,
    });
    if (dense_fits_u8) {
        candidates.push_back(Candidate{
            .encoding = CoeffTableEncoding::DenseRangeU8,
            .payload_bytes = 4u + dense_symbol_count,
        });
    }
    if (sparse_fits_u8) {
        candidates.push_back(Candidate{
            .encoding = CoeffTableEncoding::SparsePairsU8,
            .payload_bytes = 2u + nonzero_symbols.size() * 3u,
        });
    }

    Candidate best = candidates.front();
    for (size_t index = 1; index < candidates.size(); ++index) {
        if (candidates[index].payload_bytes < best.payload_bytes) {
            best = candidates[index];
        }
    }

    writer.write_u8(static_cast<uint8_t>(best.encoding));
    switch (best.encoding) {
    case CoeffTableEncoding::DenseRange:
    case CoeffTableEncoding::DenseRangeU8:
        writer.write_u16(static_cast<uint16_t>(first_nonzero));
        writer.write_u16(static_cast<uint16_t>(last_nonzero));
        for (int symbol_index = first_nonzero; symbol_index <= last_nonzero; ++symbol_index) {
            const uint16_t freq = table.symbol(symbol_index).freq;
            if (best.encoding == CoeffTableEncoding::DenseRangeU8) {
                writer.write_u8(static_cast<uint8_t>(freq));
            } else {
                writer.write_u16(freq);
            }
        }
        return {};
    case CoeffTableEncoding::SparsePairs:
    case CoeffTableEncoding::SparsePairsU8:
        writer.write_u16(static_cast<uint16_t>(nonzero_symbols.size()));
        for (int symbol_index : nonzero_symbols) {
            const uint16_t freq = table.symbol(symbol_index).freq;
            writer.write_u16(static_cast<uint16_t>(symbol_index));
            if (best.encoding == CoeffTableEncoding::SparsePairsU8) {
                writer.write_u8(static_cast<uint8_t>(freq));
            } else {
                writer.write_u16(freq);
            }
        }
        return {};
    default:
        break;
    }

    return std::unexpected(Error{ErrorCode::InvalidParameter,
                                 "coefficient table encoder selected an unsupported mode"});
}

Result<LossyCoeffTable> read_coefficient_table(ByteReader& reader,
                                               int num_symbols,
                                               std::string_view label) {
    if (num_symbols <= 0) {
        return std::unexpected(
            Error{ErrorCode::InvalidParameter, "coefficient table symbol count must be positive"});
    }

    auto encoding = reader.read_u8();
    if (!encoding) {
        return std::unexpected(encoding.error());
    }

    std::vector<uint32_t> counts(static_cast<size_t>(num_symbols), 0);
    switch (static_cast<CoeffTableEncoding>(*encoding)) {
    case CoeffTableEncoding::SingleSymbol: {
        auto symbol = reader.read_u16();
        if (!symbol) {
            return std::unexpected(symbol.error());
        }
        if (*symbol >= num_symbols) {
            return std::unexpected(invalid_table_error(label, "has invalid single-symbol index"));
        }
        counts[*symbol] = LossyCoeffTable::TABLE_SIZE;
        break;
    }
    case CoeffTableEncoding::DenseRange: {
        auto first = reader.read_u16();
        auto last = reader.read_u16();
        if (!first || !last) {
            return std::unexpected(Error{ErrorCode::TruncatedInput,
                                         std::string(label) + " coefficient table is truncated"});
        }
        if (*first > *last || *last >= num_symbols) {
            return std::unexpected(invalid_table_error(label, "has invalid dense range"));
        }
        for (uint16_t symbol_index = *first; symbol_index <= *last; ++symbol_index) {
            auto freq = reader.read_u16();
            if (!freq) {
                return std::unexpected(freq.error());
            }
            counts[symbol_index] = *freq;
        }
        break;
    }
    case CoeffTableEncoding::DenseRangeU8: {
        auto first = reader.read_u16();
        auto last = reader.read_u16();
        if (!first || !last) {
            return std::unexpected(Error{ErrorCode::TruncatedInput,
                                         std::string(label) + " coefficient table is truncated"});
        }
        if (*first > *last || *last >= num_symbols) {
            return std::unexpected(invalid_table_error(label, "has invalid dense range"));
        }
        for (uint16_t symbol_index = *first; symbol_index <= *last; ++symbol_index) {
            auto freq = reader.read_u8();
            if (!freq) {
                return std::unexpected(freq.error());
            }
            counts[symbol_index] = *freq;
        }
        break;
    }
    case CoeffTableEncoding::SparsePairs: {
        auto pair_count = reader.read_u16();
        if (!pair_count) {
            return std::unexpected(pair_count.error());
        }
        if (*pair_count == 0) {
            return std::unexpected(invalid_table_error(label, "has zero sparse pairs"));
        }
        for (uint16_t i = 0; i < *pair_count; ++i) {
            auto symbol = reader.read_u16();
            auto freq = reader.read_u16();
            if (!symbol || !freq) {
                return std::unexpected(Error{ErrorCode::TruncatedInput,
                                             std::string(label) + " coefficient table is truncated"});
            }
            if (*symbol >= num_symbols) {
                return std::unexpected(invalid_table_error(label, "has invalid sparse symbol index"));
            }
            if (*freq == 0) {
                return std::unexpected(invalid_table_error(label, "has zero sparse frequency"));
            }
            if (counts[*symbol] != 0) {
                return std::unexpected(invalid_table_error(label, "duplicates a sparse symbol"));
            }
            counts[*symbol] = *freq;
        }
        break;
    }
    case CoeffTableEncoding::SparsePairsU8: {
        auto pair_count = reader.read_u16();
        if (!pair_count) {
            return std::unexpected(pair_count.error());
        }
        if (*pair_count == 0) {
            return std::unexpected(invalid_table_error(label, "has zero sparse pairs"));
        }
        for (uint16_t i = 0; i < *pair_count; ++i) {
            auto symbol = reader.read_u16();
            auto freq = reader.read_u8();
            if (!symbol || !freq) {
                return std::unexpected(Error{ErrorCode::TruncatedInput,
                                             std::string(label) + " coefficient table is truncated"});
            }
            if (*symbol >= num_symbols) {
                return std::unexpected(invalid_table_error(label, "has invalid sparse symbol index"));
            }
            if (*freq == 0) {
                return std::unexpected(invalid_table_error(label, "has zero sparse frequency"));
            }
            if (counts[*symbol] != 0) {
                return std::unexpected(invalid_table_error(label, "duplicates a sparse symbol"));
            }
            counts[*symbol] = *freq;
        }
        break;
    }
    default:
        return std::unexpected(invalid_table_error(label, "uses an unknown encoding"));
    }

    auto counts_valid = validate_exact_counts(counts, label);
    if (!counts_valid) {
        return std::unexpected(counts_valid.error());
    }

    LossyCoeffTable table;
    table.build_from_counts(counts.data(), num_symbols);
    for (int i = 0; i < num_symbols; ++i) {
        if (table.symbol(i).freq != counts[static_cast<size_t>(i)]) {
            return std::unexpected(invalid_table_error(label, "failed exact reconstruction"));
        }
    }
    return table;
}

}
