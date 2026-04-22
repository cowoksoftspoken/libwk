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
    const size_t dense_payload_bytes = 4u + static_cast<size_t>(last_nonzero - first_nonzero + 1) * sizeof(uint16_t);
    const size_t sparse_payload_bytes = 2u + nonzero_symbols.size() * sizeof(uint16_t) * 2u;

    if (sparse_payload_bytes < dense_payload_bytes) {
        if (nonzero_symbols.size() > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(Error{ErrorCode::InvalidParameter,
                                         "coefficient table has too many sparse symbols"});
        }
        writer.write_u8(static_cast<uint8_t>(CoeffTableEncoding::SparsePairs));
        writer.write_u16(static_cast<uint16_t>(nonzero_symbols.size()));
        for (int symbol_index : nonzero_symbols) {
            writer.write_u16(static_cast<uint16_t>(symbol_index));
            writer.write_u16(table.symbol(symbol_index).freq);
        }
        return {};
    }

    writer.write_u8(static_cast<uint8_t>(CoeffTableEncoding::DenseRange));
    writer.write_u16(static_cast<uint16_t>(first_nonzero));
    writer.write_u16(static_cast<uint16_t>(last_nonzero));
    for (int symbol_index = first_nonzero; symbol_index <= last_nonzero; ++symbol_index) {
        writer.write_u16(table.symbol(symbol_index).freq);
    }
    return {};
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
