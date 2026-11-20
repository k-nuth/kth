// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/golomb_set.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <kth/infrastructure/expect.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/bit_reader.hpp>
#include <kth/infrastructure/utility/bit_writer.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth::domain::chain {

namespace {

static_assert(golomb_set_p < 64, "The Golomb-Rice remainder is shifted inside a uint64.");
static_assert(
    golomb_set_m <= std::numeric_limits<uint64_t>::max() / std::numeric_limits<uint32_t>::max(),
    "N times M fits in a uint64 for every uint32 N.");

// The reduction is the high half of the 128-bit product, which maps a hash into [0, modulus).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
uint64_t multiply_high(uint64_t value, uint64_t modulus) noexcept {
    auto const product = static_cast<unsigned __int128>(value) * modulus;
    return static_cast<uint64_t>(product >> 64);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

uint64_t hash_item(golomb_key const& key, byte_span item) {
    auto const k0 = from_little_endian_unsafe<uint64_t>(key);
    auto const k1 = from_little_endian_unsafe<uint64_t>(byte_span{key}.subspan(sizeof(uint64_t)));
    sip_hasher hasher(k0, k1);
    // An empty item is a real member. write() is only handed a pointer when there are bytes.
    if (item.size() != 0) {
        hasher.write(item.data(), item.size());
    }
    return hasher.finalize();
}

// One value of the set as it is stored: the quotient in unary (that many 1
// bits, then a 0) and the low P bits, the most significant first.
[[nodiscard]]
expect<uint64_t> read_difference(bit_reader& reader) {
    auto const quotient = reader.read_unary();
    if ( ! quotient) {
        return std::unexpected(quotient.error());
    }
    auto const remainder = reader.read_bits(golomb_set_p);
    if ( ! remainder) {
        return std::unexpected(remainder.error());
    }
    return (*quotient << golomb_set_p) + *remainder;
}

[[nodiscard]]
bool contains_count(byte_span bytes, uint32_t count) {
    bit_reader reader(bytes);
    for (uint32_t i = 0; i < count; ++i) {
        if ( ! read_difference(reader)) {
            return false;
        }
    }
    return true;
}

// The bits one difference takes in the stream.
[[nodiscard]]
constexpr size_t difference_bits(uint64_t delta) noexcept {
    return size_t(delta >> golomb_set_p) + 1 + golomb_set_p;
}

} // namespace

std::expected<golomb_set, golomb_set_error>
golomb_set::build(golomb_key const& key, std::span<data_chunk const> items) {
    // Equal byte strings are one item. N is the number of distinct items.
    std::vector<data_chunk> distinct(items.begin(), items.end());
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

    if (distinct.size() > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(golomb_set_error::too_many_items);
    }

    golomb_set set;
    set.n_ = static_cast<uint32_t>(distinct.size());
    if (set.n_ == 0) {
        return set;
    }

    auto const modulus = uint64_t{set.n_} * golomb_set_m;
    std::vector<uint64_t> values;
    values.reserve(distinct.size());
    for (auto const& item : distinct) {
        values.push_back(multiply_high(hash_item(key, item), modulus));
    }
    std::sort(values.begin(), values.end());

    // Each difference is taken from the previous reduced value. The first is
    // taken from zero. They replace the values in place.
    uint64_t previous = 0;
    size_t bits = 0;
    for (auto& value : values) {
        auto const delta = value - previous;
        previous = value;
        value = delta;
        bits += difference_bits(delta);
    }

    // The writer fills a buffer of the stream's exact size, so no write can
    // fall outside it.
    set.bits_.resize(bytes_for_bits(bits));
    bit_writer writer(set.bits_);
    for (auto const delta : values) {
        if ( ! writer.write_unary(delta >> golomb_set_p) || ! writer.write_bits(delta, golomb_set_p)) {
            return std::unexpected(golomb_set_error::too_many_items);
        }
    }
    writer.flush();
    return set;
}

std::expected<golomb_set, golomb_set_error>
golomb_set::from_data(byte_span data) {
    byte_reader reader(data);
    auto const count = reader.read_variable_little_endian();
    if ( ! count) {
        return std::unexpected(golomb_set_error::truncated);
    }
    if (*count > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(golomb_set_error::too_many_items);
    }

    auto const n = static_cast<uint32_t>(*count);
    auto const rest = data.subspan(reader.position());
    // Unread bits in the last byte are the writer's zero padding. N differences are enough.
    if ( ! contains_count(rest, n)) {
        return std::unexpected(golomb_set_error::truncated);
    }

    golomb_set set;
    set.n_ = n;
    set.bits_.assign(rest.begin(), rest.end());
    return set;
}

data_chunk golomb_set::to_data() const {
    // byte_writer writes into a buffer the caller has already sized.
    auto const prefix = size_variable_integer(uint64_t{n_});
    data_chunk out(prefix + bits_.size());
    byte_writer writer(out);
    if (auto const written = writer.write_variable_little_endian(uint64_t{n_}); ! written) {
        return {};
    }
    if (auto const written = writer.write_bytes(bits_); ! written) {
        return {};
    }
    return out;
}

uint32_t golomb_set::size() const {
    return n_;
}

bool golomb_set::match(golomb_key const& key, byte_span item) const {
    if (n_ == 0) {
        return false;
    }

    auto const modulus = uint64_t{n_} * golomb_set_m;
    auto const term = multiply_high(hash_item(key, item), modulus);
    bit_reader reader(bits_);
    uint64_t value = 0;
    for (uint32_t i = 0; i < n_; ++i) {
        auto const delta = read_difference(reader);
        if ( ! delta) {
            return false;
        }
        value += *delta;
        if (value == term) {
            return true;
        }
        if (value > term) {
            return false;
        }
    }
    return false;
}

bool golomb_set::match_any(golomb_key const& key, std::span<data_chunk const> items) const {
    if (items.empty() || n_ == 0) {
        return false;
    }
    for (auto const& item : items) {
        if (match(key, item)) {
            return true;
        }
    }
    return false;
}

} // namespace kth::domain::chain
