// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/input_prefix_filter.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/bit_reader.hpp>
#include <kth/infrastructure/utility/bit_writer.hpp>
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

    // Each value's gap from the one before, from 0 for the first, so its gap
    // is the value itself: the gap shifted right by k in one-bits, a zero-bit,
    // then its low k bits.
    size_t bits = 0;
    uint32_t previous = 0;
    for (auto const value : values) {
        bits += size_t((uint32_t{value} - previous) >> k) + 1 + k;
        previous = value;
    }

    // The writer fills a buffer of the stream's exact size, so no write can
    // fall outside it. The bits after the last value, the padding, are zero.
    auto const start = out.size();
    out.resize(start + bytes_for_bits(bits));
    bit_writer writer(std::span<uint8_t>{out}.subspan(start));
    previous = 0;
    for (auto const value : values) {
        auto const gap = uint32_t{value} - previous;
        previous = value;
        KTH_CONTRACT(writer.write_unary(gap >> k) && writer.write_bits(gap, k));
    }
    writer.flush();
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
    // Each count has one Rice parameter, so each set of values has one
    // encoding; this also refuses any parameter above 15.
    if (*k != input_prefix_rice_parameter(size_t(*count))) {
        return std::unexpected(input_prefix_filter_error::rice_parameter);
    }

    bit_reader bits(data.subspan(reader.position()));
    filter.values.reserve(size_t(*count));
    uint32_t previous = 0;
    for (uint64_t i = 0; i < *count; ++i) {
        // The quotient in one-bits up to a zero-bit, read a bit at a time: a
        // gap past the largest value is refused as soon as its quotient shows
        // it, so a long run of one-bits is not read to its end.
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
        auto const low = bits.read_bits(*k);
        if ( ! low) {
            return std::unexpected(input_prefix_filter_error::truncated);
        }
        auto const gap = (quotient << *k) | uint32_t(*low);
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
    while (bits.remaining_bits() % 8 != 0) {
        auto const one = bits.read_bit();
        if ( ! one || *one) {
            return std::unexpected(input_prefix_filter_error::padding);
        }
    }
    if ( ! bits.is_exhausted()) {
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
