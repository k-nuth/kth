// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/input_prefix_filter.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::chain {

namespace {

static_assert(max_input_prefix_rice_parameter < 32, "A gap and its low bits fit in a uint32.");

// The count as a CompactSize, in its shortest form. A filter holds at most
// 65,536 values, so four bytes after the marker always suffice.
void append_count(data_chunk& out, size_t count) {
    if (count < 0xfd) {
        out.push_back(uint8_t(count));
        return;
    }
    auto const width = count <= 0xffff ? 2 : 4;
    out.push_back(width == 2 ? 0xfd : 0xfe);
    for (int i = 0; i < width; ++i) {
        out.push_back(uint8_t(count >> (8 * i)));
    }
}

// Most significant bit first. A fresh byte is zero, so the bits after the
// last one written, the padding, are zero.
struct bit_writer {
    data_chunk& bytes;
    uint8_t space = 0;

    void write_bit(bool one) {
        if (space == 0) {
            bytes.push_back(0);
            space = 8;
        }
        --space;
        if (one) {
            bytes.back() = uint8_t(bytes.back() | (1u << space));
        }
    }
};

struct bit_reader {
    byte_span bytes;
    size_t position = 0;

    [[nodiscard]]
    std::optional<bool> read_bit() {
        if (position >= bytes.size() * 8) {
            return std::nullopt;
        }
        auto const byte = bytes[position / 8];
        auto const one = ((byte >> (7 - position % 8)) & 1u) != 0;
        ++position;
        return one;
    }
};

} // namespace

uint16_t input_prefix(input const& in) {
    // The input as it stands in its transaction: the outpoint, the script
    // with its length, and the sequence.
    auto const hash = bitcoin_hash(to_data_chunk(in, true));
    return uint16_t((hash[0] << 8) | hash[1]);
}

input_prefix_filter build_input_prefix_filter(block const& blk) {
    input_prefix_filter filter;
    auto const& txs = blk.transactions();
    // The first transaction is the coinbase: its input spends nothing a
    // sender could grind.
    for (size_t i = 1; i < txs.size(); ++i) {
        auto const& inputs = txs[i].inputs();
        auto const used = std::min(inputs.size(), input_prefix_inputs);
        for (size_t j = 0; j < used; ++j) {
            filter.values.push_back(input_prefix(inputs[j]));
        }
    }
    std::ranges::sort(filter.values);
    auto const repeated = std::ranges::unique(filter.values);
    filter.values.erase(repeated.begin(), repeated.end());
    return filter;
}

uint8_t input_prefix_rice_parameter(size_t count) {
    // A filter with no values carries no parameter.
    if (count == 0) {
        return 0;
    }
    auto const quotient = input_prefix_rice_scale / count;
    if (quotient == 0) {
        return 0;
    }
    return uint8_t(std::bit_width(quotient) - 1);
}

data_chunk to_data(input_prefix_filter const& filter) {
    // Encoded as the set it is: sorted, each value once, whatever order the
    // caller gave.
    auto values = filter.values;
    std::ranges::sort(values);
    auto const repeated = std::ranges::unique(values);
    values.erase(repeated.begin(), repeated.end());

    data_chunk out;
    append_count(out, values.size());
    if (values.empty()) {
        return out;
    }
    auto const k = input_prefix_rice_parameter(values.size());
    out.push_back(k);

    bit_writer writer{out};
    uint32_t previous = 0;
    for (auto const value : values) {
        // From 0 for the first value, so its gap is the value itself.
        auto const gap = uint32_t{value} - previous;
        previous = value;
        for (auto quotient = gap >> k; quotient != 0; --quotient) {
            writer.write_bit(true);
        }
        writer.write_bit(false);
        for (auto bit = int{k}; bit > 0; --bit) {
            writer.write_bit(((gap >> (bit - 1)) & 1u) != 0);
        }
    }
    return out;
}

std::expected<input_prefix_filter, input_prefix_filter_error>
parse_input_prefix_filter(byte_span data) {
    byte_reader reader(data);
    auto const count = reader.read_variable_little_endian();
    if ( ! count) {
        // Cut short, or not in its shortest form.
        return std::unexpected(count.error() == error::invalid_size
            ? input_prefix_filter_error::count : input_prefix_filter_error::truncated);
    }
    // Refused before anything is reserved for them: more values than there
    // are cannot all be distinct.
    if (*count > input_prefix_values) {
        return std::unexpected(input_prefix_filter_error::count);
    }
    input_prefix_filter filter;
    if (*count == 0) {
        if ( ! reader.is_exhausted()) {
            return std::unexpected(input_prefix_filter_error::trailing_bytes);
        }
        return filter;
    }

    auto const k = reader.read_byte();
    if ( ! k) {
        return std::unexpected(input_prefix_filter_error::truncated);
    }
    if (*k > max_input_prefix_rice_parameter) {
        return std::unexpected(input_prefix_filter_error::rice_parameter);
    }

    bit_reader bits{data.subspan(reader.position())};
    filter.values.reserve(size_t(*count));
    uint32_t previous = 0;
    for (uint64_t i = 0; i < *count; ++i) {
        // The quotient in one-bits up to a zero-bit. A gap past the largest
        // value is refused as soon as its quotient shows it, so a long run of
        // one-bits is not read to its end.
        uint32_t quotient = 0;
        for (;;) {
            auto const one = bits.read_bit();
            if ( ! one) {
                return std::unexpected(input_prefix_filter_error::truncated);
            }
            if ( ! *one) {
                break;
            }
            ++quotient;
            if (previous + (quotient << *k) >= input_prefix_values) {
                return std::unexpected(input_prefix_filter_error::value_too_large);
            }
        }
        uint32_t low = 0;
        for (auto bit = *k; bit > 0; --bit) {
            auto const one = bits.read_bit();
            if ( ! one) {
                return std::unexpected(input_prefix_filter_error::truncated);
            }
            low = (low << 1) | (*one ? 1u : 0u);
        }
        auto const gap = (quotient << *k) | low;
        // Each value is above the one before; only the first may be 0.
        if (i != 0 && gap == 0) {
            return std::unexpected(input_prefix_filter_error::repeated_value);
        }
        auto const value = previous + gap;
        if (value >= input_prefix_values) {
            return std::unexpected(input_prefix_filter_error::value_too_large);
        }
        filter.values.push_back(uint16_t(value));
        previous = value;
    }

    // The rest of the last byte is padding, all zeros, and nothing follows it.
    while (bits.position % 8 != 0) {
        if (*bits.read_bit()) {
            return std::unexpected(input_prefix_filter_error::padding);
        }
    }
    if (bits.position != bits.bytes.size() * 8) {
        return std::unexpected(input_prefix_filter_error::trailing_bytes);
    }
    return filter;
}

std::expected<bool, input_prefix_filter_error>
has_input_prefix(input_prefix_filter const& filter, uint16_t prefix, uint8_t bits) {
    if (bits > 16 || uint32_t{prefix} >= (uint32_t{1} << bits)) {
        return std::unexpected(input_prefix_filter_error::prefix);
    }
    auto const shift = 16 - bits;
    auto const low = uint32_t{prefix} << shift;
    auto const high = (uint32_t{prefix} + 1) << shift;
    // The values are sorted: the first one at or above `low` decides.
    auto const found = std::ranges::lower_bound(filter.values, low, {}, [](uint16_t value) {
        return uint32_t{value};
    });
    return found != filter.values.end() && uint32_t{*found} < high;
}

hash_digest input_prefix_filter_hash(input_prefix_filter const& filter) {
    return bitcoin_hash(to_data(filter));
}

hash_digest input_prefix_filter_header(input_prefix_filter const& filter, hash_digest const& previous_header) {
    return bitcoin_hash(splice(input_prefix_filter_hash(filter), previous_header));
}

} // namespace kth::domain::chain
