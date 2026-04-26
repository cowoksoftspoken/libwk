#include "lossy_coeff_stream.h"

#include "coeff_presence_stream.h"
#include "coeff_table_bank_stream.h"
#include "coeff_sign_stream.h"
#include "coeff_table_stream.h"
#include "rans.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace wk {

namespace {

constexpr int kSignedOffset = 1024;
constexpr int kSignedNumSymbols = 2049;
constexpr int kMagnitudeNumSymbols = 1025;

struct SymbolCoding {
    int num_symbols = kSignedNumSymbols;
    bool split_magnitude_signs = false;

    [[nodiscard]] int encode(int value) const {
        if (split_magnitude_signs) {
            return std::clamp(std::abs(value), 0, kMagnitudeNumSymbols - 1);
        }
        return std::clamp(value + kSignedOffset, 0, kSignedNumSymbols - 1);
    }

    [[nodiscard]] int16_t decode(int symbol) const {
        if (split_magnitude_signs) {
            return static_cast<int16_t>(symbol);
        }
        return static_cast<int16_t>(symbol - kSignedOffset);
    }
};

struct EncodedCoeffContextStream {
    std::vector<uint8_t> presence_bytes;
    std::vector<uint8_t> rans_bytes;
    std::vector<uint8_t> sign_bits;
    size_t active_blocks = 0;
    size_t nonzero_count = 0;
};

struct CoeffContextAnalysis {
    std::vector<uint8_t> presence;
    size_t active_blocks = 0;
    size_t nonzero_count = 0;
};

[[nodiscard]] SymbolCoding make_symbol_coding(const LossyCoeffStreamConfig& config) {
    SymbolCoding coding;
    coding.split_magnitude_signs = config.split_magnitude_signs;
    coding.num_symbols = config.split_magnitude_signs ? kMagnitudeNumSymbols : kSignedNumSymbols;
    return coding;
}

[[nodiscard]] int coefficient_limit(uint8_t max_coeff_span, const LossyCoeffStreamConfig& config) {
    return config.use_plane_max_coeff_span ? max_coeff_span : 64;
}

[[nodiscard]] size_t count_active_blocks(std::span<const uint8_t> spans, int coeff_index) {
    size_t active = 0;
    for (uint8_t span_value : spans) {
        active += span_value > coeff_index ? 1u : 0u;
    }
    return active;
}

[[nodiscard]] CoeffContextAnalysis analyze_coeff_context(std::span<const DctBlockI16> blocks,
                                                         std::span<const uint8_t> spans,
                                                         int coeff_index,
                                                         bool keep_presence) {
    CoeffContextAnalysis analysis;
    if (keep_presence) {
        analysis.presence.reserve(blocks.size());
    }

    for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        if (spans[block_index] <= coeff_index) {
            continue;
        }
        const bool nonzero = blocks[block_index][coeff_index] != 0;
        if (keep_presence) {
            analysis.presence.push_back(static_cast<uint8_t>(nonzero));
        }
        ++analysis.active_blocks;
        analysis.nonzero_count += nonzero ? 1u : 0u;
    }

    return analysis;
}

[[nodiscard]] LossyCoeffTable make_single_symbol_table(const SymbolCoding& coding) {
    std::vector<uint32_t> counts(static_cast<size_t>(coding.num_symbols), 0);
    counts[static_cast<size_t>(coding.split_magnitude_signs ? 0 : kSignedOffset)] = 1;
    LossyCoeffTable table;
    table.build_from_counts(counts.data(), coding.num_symbols);
    return table;
}

[[nodiscard]] std::optional<int> single_symbol_from_table(const LossyCoeffTable& table,
                                                          int num_symbols) {
    int symbol_index = -1;
    for (int symbol = 0; symbol < num_symbols; ++symbol) {
        const uint16_t freq = table.symbol(symbol).freq;
        if (freq == 0) {
            continue;
        }
        if (freq != LossyCoeffTable::TABLE_SIZE || symbol_index >= 0) {
            return std::nullopt;
        }
        symbol_index = symbol;
    }
    if (symbol_index < 0) {
        return std::nullopt;
    }
    return symbol_index;
}

[[nodiscard]] Result<void> write_legacy_coeff_table(ByteWriter& writer,
                                                    const LossyCoeffTable& table,
                                                    int num_symbols) {
    int first_nonzero = -1;
    int last_nonzero = -1;
    for (int symbol = 0; symbol < num_symbols; ++symbol) {
        if (table.symbol(symbol).freq == 0) {
            continue;
        }
        if (first_nonzero < 0) {
            first_nonzero = symbol;
        }
        last_nonzero = symbol;
    }

    if (first_nonzero < 0) {
        return std::unexpected(
            Error{ErrorCode::InvalidParameter, "coefficient table must contain at least one symbol"});
    }

    writer.write_u16(static_cast<uint16_t>(first_nonzero));
    writer.write_u16(static_cast<uint16_t>(last_nonzero));
    for (int symbol = first_nonzero; symbol <= last_nonzero; ++symbol) {
        writer.write_u16(table.symbol(symbol).freq);
    }
    return {};
}

[[nodiscard]] Result<void> write_lossy_coeff_table(ByteWriter& writer,
                                                   const LossyCoeffTable& table,
                                                   int num_symbols,
                                                   const LossyCoeffStreamConfig& config) {
    if (config.adaptive_coefficient_tables) {
        return write_coefficient_table(writer, table, num_symbols);
    }
    return write_legacy_coeff_table(writer, table, num_symbols);
}

[[nodiscard]] Result<EncodedCoeffContextStream> encode_coeff_context_stream(
    std::span<const DctBlockI16> blocks,
    std::span<const uint8_t> spans,
    int coeff_index,
    const CoeffContextAnalysis& analysis,
    const LossyCoeffTable& table,
    const SymbolCoding& coding,
    const LossyCoeffStreamConfig& config) {
    EncodedCoeffContextStream stream;
    stream.active_blocks = analysis.active_blocks;
    stream.nonzero_count = config.use_significance_maps ? analysis.nonzero_count : analysis.active_blocks;

    if (config.use_significance_maps && analysis.active_blocks > 0) {
        ByteWriter presence_writer;
        auto written = write_adaptive_coefficient_presence(presence_writer, analysis.presence);
        if (!written) {
            return std::unexpected(written.error());
        }
        stream.presence_bytes = presence_writer.finish();
    }

    if (stream.nonzero_count == 0) {
        return stream;
    }

    const bool elide_rans_stream = config.elide_single_symbol_streams &&
        single_symbol_from_table(table, coding.num_symbols).has_value();

    if (!elide_rans_stream) {
        RansEncoder<RANS_PRECISION_BITS> encoder;
        encoder.init();

        for (size_t reverse_index = blocks.size(); reverse_index > 0; --reverse_index) {
            const size_t block_index = reverse_index - 1;
            if (spans[block_index] <= coeff_index) {
                continue;
            }
            const int value = static_cast<int>(blocks[block_index][coeff_index]);
            if (config.use_significance_maps && value == 0) {
                continue;
            }
            encoder.encode(table, coding.encode(value));
        }
        stream.rans_bytes = encoder.finish();
    }

    if (coding.split_magnitude_signs) {
        std::vector<uint8_t> signs;
        signs.reserve(blocks.size());
        for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            if (spans[block_index] <= coeff_index) {
                continue;
            }
            const int value = static_cast<int>(blocks[block_index][coeff_index]);
            if (value != 0) {
                signs.push_back(static_cast<uint8_t>(value < 0));
            }
        }
        auto packed_signs = pack_coefficient_signs(signs);
        if (!packed_signs) {
            return std::unexpected(packed_signs.error());
        }
        stream.sign_bits = std::move(*packed_signs);
    }

    return stream;
}

[[nodiscard]] Result<void> write_coeff_context_stream(
    ByteWriter& writer,
    const EncodedCoeffContextStream& stream,
    const LossyCoeffTable& table,
    const SymbolCoding& coding,
    const LossyCoeffStreamConfig& config) {
    if (config.use_significance_maps && stream.active_blocks > 0) {
        writer.write_bytes(stream.presence_bytes);
    }
    if (stream.nonzero_count == 0) {
        if (!config.use_significance_maps) {
            writer.write_u32(0);
        }
        return {};
    }

    const bool elide_rans_stream = config.elide_single_symbol_streams &&
        stream.nonzero_count > 0 &&
        single_symbol_from_table(table, coding.num_symbols).has_value();
    if (!elide_rans_stream) {
        writer.write_u32(static_cast<uint32_t>(stream.rans_bytes.size()));
        writer.write_bytes(stream.rans_bytes);
    }
    writer.write_bytes(stream.sign_bits);
    return {};
}

[[nodiscard]] Result<size_t> decode_elided_single_symbol_stream(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    int coeff_index,
    const SymbolCoding& coding,
    int symbol,
    std::span<const uint8_t> presence,
    std::span<DctBlockI16> blocks,
    std::string_view label) {
    const int16_t value = coding.decode(symbol);
    size_t present_blocks = 0;
    size_t presence_index = 0;
    for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        if (spans[block_index] <= coeff_index) {
            continue;
        }
        const bool is_present = presence.empty() || presence[presence_index++] != 0;
        if (!is_present) {
            blocks[block_index][coeff_index] = 0;
            continue;
        }
        blocks[block_index][coeff_index] = value;
        ++present_blocks;
    }

    if (coding.split_magnitude_signs && value != 0) {
        auto sign_bits = read_packed_coefficient_signs(reader, present_blocks, label);
        if (!sign_bits) {
            return std::unexpected(sign_bits.error());
        }

        size_t sign_index = 0;
        presence_index = 0;
        for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            if (spans[block_index] <= coeff_index) {
                continue;
            }
            const bool is_present = presence.empty() || presence[presence_index++] != 0;
            if (!is_present) {
                continue;
            }
            if ((*sign_bits)[sign_index++] != 0) {
                blocks[block_index][coeff_index] = static_cast<int16_t>(-value);
            }
        }
    }

    return present_blocks;
}

[[nodiscard]] Result<EncodedCoeffContextStream> read_coeff_context_stream(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    int coeff_index,
    const LossyCoeffTable& table,
    const SymbolCoding& coding,
    std::span<DctBlockI16> blocks,
    std::string_view label,
    const LossyCoeffStreamConfig& config) {
    EncodedCoeffContextStream stream;
    const size_t active_blocks = count_active_blocks(spans, coeff_index);
    stream.active_blocks = active_blocks;

    if (active_blocks == 0) {
        if (!config.use_significance_maps) {
            auto encoded_size = reader.read_u32();
            if (!encoded_size) {
                return std::unexpected(encoded_size.error());
            }
            if (*encoded_size != 0) {
                return std::unexpected(Error{ErrorCode::RansError,
                                             "unexpected data for empty coefficient stream"});
            }
        }
        return stream;
    }

    std::vector<uint8_t> presence;
    if (config.use_significance_maps) {
        auto presence_read = read_adaptive_coefficient_presence(reader, active_blocks, label);
        if (!presence_read) {
            return std::unexpected(presence_read.error());
        }
        presence = std::move(*presence_read);
        stream.nonzero_count = std::count(presence.begin(), presence.end(), static_cast<uint8_t>(1));
        if (stream.nonzero_count == 0) {
            return stream;
        }
    } else {
        stream.nonzero_count = active_blocks;
    }

    if (config.elide_single_symbol_streams) {
        auto symbol = single_symbol_from_table(table, coding.num_symbols);
        if (symbol) {
            auto decoded = decode_elided_single_symbol_stream(reader, spans, coeff_index, coding,
                                                              *symbol, presence, blocks, label);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            stream.nonzero_count = *decoded;
            return stream;
        }
    }

    auto encoded_size = reader.read_u32();
    if (!encoded_size) {
        return std::unexpected(encoded_size.error());
    }
    auto encoded = reader.read_bytes(*encoded_size);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }

    RansDecoder<RANS_PRECISION_BITS> decoder;
    decoder.init(encoded->data(), encoded->size());
    if (!decoder.ok()) {
        return std::unexpected(Error{ErrorCode::RansError, "invalid rANS stream header"});
    }

    size_t nonzero_count = 0;
    size_t presence_index = 0;
    for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        if (spans[block_index] <= coeff_index) {
            continue;
        }
        if (!presence.empty() && presence[presence_index++] == 0) {
            blocks[block_index][coeff_index] = 0;
            continue;
        }
        const int symbol = decoder.decode(table);
        if (!decoder.ok()) {
            return std::unexpected(Error{ErrorCode::RansError, "corrupt rANS stream"});
        }
        blocks[block_index][coeff_index] = coding.decode(symbol);
        nonzero_count += blocks[block_index][coeff_index] != 0 ? 1u : 0u;
    }

    if (coding.split_magnitude_signs) {
        auto sign_bits = read_packed_coefficient_signs(reader, nonzero_count, label);
        if (!sign_bits) {
            return std::unexpected(sign_bits.error());
        }

        size_t sign_index = 0;
        presence_index = 0;
        for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            if (spans[block_index] <= coeff_index) {
                continue;
            }
            if (!presence.empty() && presence[presence_index++] == 0) {
                continue;
            }
            auto& value = blocks[block_index][coeff_index];
            if (value == 0) {
                continue;
            }
            if ((*sign_bits)[sign_index++] != 0) {
                value = static_cast<int16_t>(-value);
            }
        }
    }

    return stream;
}

[[nodiscard]] Result<LossyCoeffTable> read_lossy_coeff_table(
    ByteReader& reader,
    const SymbolCoding& coding,
    const LossyCoeffStreamConfig& config) {
    if (config.adaptive_coefficient_tables) {
        return read_coefficient_table(reader, coding.num_symbols, "lossy");
    }

    auto first_read = reader.read_u16();
    auto last_read = reader.read_u16();
    if (!first_read || !last_read) {
        return std::unexpected(Error{ErrorCode::TruncatedInput, "missing rANS symbol range"});
    }

    const int first = *first_read;
    const int last = *last_read;
    if (first > last || first < 0 || last >= coding.num_symbols) {
        return std::unexpected(Error{ErrorCode::RansError, "invalid rANS symbol range"});
    }

    std::vector<uint32_t> counts(static_cast<size_t>(coding.num_symbols), 0);
    for (int symbol = first; symbol <= last; ++symbol) {
        auto freq = reader.read_u16();
        if (!freq) {
            return std::unexpected(freq.error());
        }
        counts[static_cast<size_t>(symbol)] = *freq;
    }

    LossyCoeffTable table;
    table.build_from_counts(counts.data(), coding.num_symbols);
    return table;
}

[[nodiscard]] std::vector<uint32_t> build_plane_counts(
    std::span<const DctBlockI16> blocks,
    std::span<const uint8_t> spans,
    int coeff_index,
    const SymbolCoding& coding,
    bool skip_zeros) {
    std::vector<uint32_t> counts(static_cast<size_t>(coding.num_symbols), 0);
    for (size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        if (spans[block_index] <= coeff_index) {
            continue;
        }
        const int value = static_cast<int>(blocks[block_index][coeff_index]);
        if (skip_zeros && value == 0) {
            continue;
        }
        counts[static_cast<size_t>(coding.encode(value))]++;
    }
    return counts;
}

[[nodiscard]] std::vector<uint32_t> build_chroma_counts(
    std::span<const DctBlockI16> cb_blocks,
    std::span<const DctBlockI16> cr_blocks,
    std::span<const uint8_t> spans,
    int coeff_index,
    const SymbolCoding& coding,
    bool skip_zeros) {
    std::vector<uint32_t> counts(static_cast<size_t>(coding.num_symbols), 0);
    for (size_t block_index = 0; block_index < cb_blocks.size(); ++block_index) {
        if (spans[block_index] <= coeff_index) {
            continue;
        }
        const int cb_value = static_cast<int>(cb_blocks[block_index][coeff_index]);
        const int cr_value = static_cast<int>(cr_blocks[block_index][coeff_index]);
        if (!skip_zeros || cb_value != 0) {
            counts[static_cast<size_t>(coding.encode(cb_value))]++;
        }
        if (!skip_zeros || cr_value != 0) {
            counts[static_cast<size_t>(coding.encode(cr_value))]++;
        }
    }
    return counts;
}

}

Result<std::vector<uint8_t>> encode_lossy_plane_payload(
    std::span<const DctBlockI16> blocks,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config) {
    if (config.use_table_bank && !config.adaptive_coefficient_tables) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "coefficient table bank requires adaptive coefficient tables"});
    }
    if (blocks.size() != spans.size()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "lossy plane coefficient blocks and spans must match"});
    }

    const SymbolCoding coding = make_symbol_coding(config);
    const int coeff_limit = coefficient_limit(max_coeff_span, config);
    ByteWriter writer;
    std::vector<LossyCoeffTable> tables;
    std::vector<EncodedCoeffContextStream> streams;
    tables.reserve(static_cast<size_t>(coeff_limit));
    streams.reserve(static_cast<size_t>(coeff_limit));

    for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
        const CoeffContextAnalysis analysis =
            analyze_coeff_context(blocks, spans, coeff_index, config.use_significance_maps);
        if (analysis.active_blocks == 0) {
            tables.push_back(make_single_symbol_table(coding));
            EncodedCoeffContextStream empty_stream;
            streams.push_back(std::move(empty_stream));
            continue;
        }

        auto counts = build_plane_counts(blocks, spans, coeff_index, coding, config.use_significance_maps);
        LossyCoeffTable table;
        if (config.use_significance_maps && analysis.nonzero_count == 0) {
            table = make_single_symbol_table(coding);
        } else {
            table.build_from_counts(counts.data(), coding.num_symbols);
        }
        auto encoded_stream = encode_coeff_context_stream(blocks, spans, coeff_index, analysis,
                                                          table, coding, config);
        if (!encoded_stream) {
            return std::unexpected(encoded_stream.error());
        }
        tables.push_back(std::move(table));
        streams.push_back(std::move(*encoded_stream));
    }

    if (config.use_table_bank) {
        auto bank_result = write_coefficient_table_bank(writer, tables, coding.num_symbols);
        if (!bank_result) {
            return std::unexpected(bank_result.error());
        }
    }

    for (size_t context_index = 0; context_index < tables.size(); ++context_index) {
        if (!config.use_table_bank) {
            auto table_result = write_lossy_coeff_table(writer, tables[context_index], coding.num_symbols, config);
            if (!table_result) {
                return std::unexpected(table_result.error());
            }
        }
        auto write_result = write_coeff_context_stream(writer, streams[context_index],
                                                       tables[context_index], coding, config);
        if (!write_result) {
            return std::unexpected(write_result.error());
        }
    }

    return writer.finish();
}

Result<std::vector<uint8_t>> encode_lossy_chroma_payload(
    std::span<const DctBlockI16> cb_blocks,
    std::span<const DctBlockI16> cr_blocks,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config) {
    if (config.use_table_bank && !config.adaptive_coefficient_tables) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "coefficient table bank requires adaptive coefficient tables"});
    }
    if (cb_blocks.size() != spans.size() || cr_blocks.size() != spans.size()) {
        return std::unexpected(Error{ErrorCode::InvalidParameter,
                                     "lossy chroma coefficient blocks and spans must match"});
    }

    const SymbolCoding coding = make_symbol_coding(config);
    const int coeff_limit = coefficient_limit(max_coeff_span, config);
    ByteWriter writer;
    std::vector<LossyCoeffTable> tables;
    std::vector<EncodedCoeffContextStream> cb_streams;
    std::vector<EncodedCoeffContextStream> cr_streams;
    tables.reserve(static_cast<size_t>(coeff_limit));
    cb_streams.reserve(static_cast<size_t>(coeff_limit));
    cr_streams.reserve(static_cast<size_t>(coeff_limit));

    for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
        const CoeffContextAnalysis cb_analysis =
            analyze_coeff_context(cb_blocks, spans, coeff_index, config.use_significance_maps);
        const CoeffContextAnalysis cr_analysis =
            analyze_coeff_context(cr_blocks, spans, coeff_index, config.use_significance_maps);
        if (cb_analysis.active_blocks == 0) {
            tables.push_back(make_single_symbol_table(coding));
            EncodedCoeffContextStream empty_stream;
            cb_streams.push_back(empty_stream);
            cr_streams.push_back(std::move(empty_stream));
            continue;
        }

        auto counts = build_chroma_counts(cb_blocks, cr_blocks, spans, coeff_index, coding,
                                          config.use_significance_maps);
        LossyCoeffTable table;
        if (config.use_significance_maps &&
            cb_analysis.nonzero_count + cr_analysis.nonzero_count == 0) {
            table = make_single_symbol_table(coding);
        } else {
            table.build_from_counts(counts.data(), coding.num_symbols);
        }
        auto cb_stream = encode_coeff_context_stream(cb_blocks, spans, coeff_index, cb_analysis,
                                                     table, coding, config);
        if (!cb_stream) {
            return std::unexpected(cb_stream.error());
        }
        auto cr_stream = encode_coeff_context_stream(cr_blocks, spans, coeff_index, cr_analysis,
                                                     table, coding, config);
        if (!cr_stream) {
            return std::unexpected(cr_stream.error());
        }
        tables.push_back(std::move(table));
        cb_streams.push_back(std::move(*cb_stream));
        cr_streams.push_back(std::move(*cr_stream));
    }

    if (config.use_table_bank) {
        auto bank_result = write_coefficient_table_bank(writer, tables, coding.num_symbols);
        if (!bank_result) {
            return std::unexpected(bank_result.error());
        }
    }

    for (size_t context_index = 0; context_index < tables.size(); ++context_index) {
        if (!config.use_table_bank) {
            auto table_result = write_lossy_coeff_table(writer, tables[context_index], coding.num_symbols, config);
            if (!table_result) {
                return std::unexpected(table_result.error());
            }
        }
        auto write_cb = write_coeff_context_stream(writer, cb_streams[context_index],
                                                   tables[context_index], coding, config);
        if (!write_cb) {
            return std::unexpected(write_cb.error());
        }
        auto write_cr = write_coeff_context_stream(writer, cr_streams[context_index],
                                                   tables[context_index], coding, config);
        if (!write_cr) {
            return std::unexpected(write_cr.error());
        }
    }

    return writer.finish();
}

Result<std::vector<DctBlockI16>> decode_lossy_plane_payload(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config) {
    if (config.use_table_bank && !config.adaptive_coefficient_tables) {
        return std::unexpected(Error{ErrorCode::DecodeFailed,
                                     "coefficient table bank requires adaptive coefficient tables"});
    }
    const SymbolCoding coding = make_symbol_coding(config);
    const int coeff_limit = coefficient_limit(max_coeff_span, config);
    std::vector<DctBlockI16> blocks(spans.size());

    if (config.use_table_bank) {
        auto tables = read_coefficient_table_bank(reader, static_cast<size_t>(coeff_limit),
                                                  coding.num_symbols, "lossy");
        if (!tables) {
            return std::unexpected(tables.error());
        }
        for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
            auto stream = read_coeff_context_stream(reader, spans, coeff_index,
                                                    (*tables)[static_cast<size_t>(coeff_index)],
                                                    coding, blocks, "lossy", config);
            if (!stream) {
                return std::unexpected(stream.error());
            }
        }
        return blocks;
    }

    for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
        auto table = read_lossy_coeff_table(reader, coding, config);
        if (!table) {
            return std::unexpected(table.error());
        }
        auto stream = read_coeff_context_stream(reader, spans, coeff_index, *table, coding, blocks, "lossy", config);
        if (!stream) {
            return std::unexpected(stream.error());
        }
    }

    return blocks;
}

Result<DecodedLossyChromaPayload> decode_lossy_chroma_payload(
    ByteReader& reader,
    std::span<const uint8_t> spans,
    uint8_t max_coeff_span,
    const LossyCoeffStreamConfig& config) {
    if (config.use_table_bank && !config.adaptive_coefficient_tables) {
        return std::unexpected(Error{ErrorCode::DecodeFailed,
                                     "coefficient table bank requires adaptive coefficient tables"});
    }
    const SymbolCoding coding = make_symbol_coding(config);
    const int coeff_limit = coefficient_limit(max_coeff_span, config);

    DecodedLossyChromaPayload payload;
    payload.cb_blocks.resize(spans.size());
    payload.cr_blocks.resize(spans.size());

    if (config.use_table_bank) {
        auto tables = read_coefficient_table_bank(reader, static_cast<size_t>(coeff_limit),
                                                  coding.num_symbols, "lossy");
        if (!tables) {
            return std::unexpected(tables.error());
        }
        for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
            const auto& table = (*tables)[static_cast<size_t>(coeff_index)];
            auto cb_stream = read_coeff_context_stream(reader, spans, coeff_index, table, coding,
                                                       payload.cb_blocks, "lossy", config);
            if (!cb_stream) {
                return std::unexpected(cb_stream.error());
            }

            auto cr_stream = read_coeff_context_stream(reader, spans, coeff_index, table, coding,
                                                       payload.cr_blocks, "lossy", config);
            if (!cr_stream) {
                return std::unexpected(cr_stream.error());
            }
        }
        return payload;
    }

    for (int coeff_index = 0; coeff_index < coeff_limit; ++coeff_index) {
        auto table = read_lossy_coeff_table(reader, coding, config);
        if (!table) {
            return std::unexpected(table.error());
        }

        auto cb_stream = read_coeff_context_stream(reader, spans, coeff_index, *table, coding,
                                                   payload.cb_blocks, "lossy", config);
        if (!cb_stream) {
            return std::unexpected(cb_stream.error());
        }

        auto cr_stream = read_coeff_context_stream(reader, spans, coeff_index, *table, coding,
                                                   payload.cr_blocks, "lossy", config);
        if (!cr_stream) {
            return std::unexpected(cr_stream.error());
        }
    }

    return payload;
}

}
