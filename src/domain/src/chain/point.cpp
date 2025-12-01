// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/point.hpp>

#include <cstdint>
#include <sstream>
#include <utility>

// #include <kth/domain/constants.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <kth/infrastructure/utility/serializer.hpp>

namespace kth::domain::chain {

constexpr auto store_point_size = std::tuple_size<point>::value;

// Constructors.
//-----------------------------------------------------------------------------

// A default instance is invalid (until modified).
// constexpr
point::point()
    : hash_(null_hash)
{}

// constexpr
point::point(hash_digest const& hash, uint32_t index)
    : hash_(hash), index_(index), valid_(true) {}

// Operators.
//-----------------------------------------------------------------------------

// constexpr    //Note(kth): Could be constexpr in C++20
bool operator==(point const& x, point const& y) {
    return (x.hash_ == y.hash_) && (x.index_ == y.index_);
}

// constexpr
bool operator!=(point const& x, point const& y) {
    return !(x == y);
}

// This arbitrary order is produced to support set uniqueness determinations.
// constexpr
bool operator<(point const& x, point const& y) {
    // The index is primary only because its comparisons are simpler.
    return x.index_ == y.index_ ? x.hash_ < y.hash_ : x.index_ < y.index_;
}

// constexpr
bool operator>(point const& x, point const& y) {
    return y < x;
}

// constexpr
bool operator<=(point const& x, point const& y) {
    return !(y < x);
}

// constexpr
bool operator>=(point const& x, point const& y) {
    return !(x < y);
}

// protected
void point::reset() {
    valid_ = false;
    hash_ = null_hash;
    index_ = 0;
}

// constexpr
bool point::is_valid() const {
    return valid_ || (hash_ != null_hash) || (index_ != 0);
}

// Deserialization.
//-----------------------------------------------------------------------------


expect<point> point::from_data(byte_reader& reader, bool wire) {
    auto const hash = read_hash(reader);
    if ( ! hash) {
        return std::unexpected(hash.error());
    }

    if ( ! wire) {
        auto const index = reader.read_little_endian<uint16_t>();
        if ( ! index) {
            return std::unexpected(index.error());
        }
        if (*index == max_uint16) {
            return point {*hash, null_index};
        }
        return point {*hash, *index};
    }

    auto const index = reader.read_little_endian<uint32_t>();
    if ( ! index) {
        return std::unexpected(index.error());
    }
    return point {*hash, *index};
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk point::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void point::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Iterator.
//-----------------------------------------------------------------------------

point_iterator point::begin() const {
    return {*this};
}

point_iterator point::end() const {
    return {*this, static_cast<unsigned>(store_point_size)};
}

// Properties.
//-----------------------------------------------------------------------------

// constexpr
size_t point::serialized_size(bool wire /*= true*/) const {
    return wire ? point::satoshi_fixed_size() : store_point_size;
}

// constexpr
size_t point::satoshi_fixed_size() {
    return hash_size + sizeof(index_);
}

// constexpr
hash_digest& point::hash() {
    return hash_;
}

// constexpr
hash_digest const& point::hash() const {
    return hash_;
}

// constexpr
void point::set_hash(hash_digest const& value) {
    // This is no longer a default instance, so valid.
    valid_ = true;
    hash_ = value;
}

// constexpr
uint32_t point::index() const {
    return index_;
}

// constexpr
void point::set_index(uint32_t value) {
    // This is no longer a default instance, so valid.
    valid_ = true;
    index_ = value;
}

// Utilities.
//-----------------------------------------------------------------------------

// Changed in v3.0 and again in v3.1 (3.0 was unmasked, lots of collisions).
// This is used with output_point identification within a set of history rows
// of the same address. Collision will result in miscorrelation of points by
// client callers. This is stored in database. This is NOT a bitcoin checksum.
uint64_t point::checksum() const {
    // Reserve 49 bits for the tx hash and 15 bits (32768) for the input index.
    static constexpr uint64_t mask = 0xffffffffffff8000;

    // Use an offset to the middle of the hash to avoid coincidental mining
    // of values into the front or back of tx hash (not a security feature).
    // Use most possible bits of tx hash to make intentional collision hard.
    auto const tx = from_little_endian_unsafe<uint64_t>(std::span{hash_}.subspan(12));
    auto const index = static_cast<uint64_t>(index_);

    auto const tx_upper_49_bits = tx & mask;
    auto const index_lower_15_bits = index & ~mask;
    return tx_upper_49_bits | index_lower_15_bits;
}

// Validation.
//-----------------------------------------------------------------------------

// constexpr
bool point::is_null() const {
    return (index_ == null_index) && (hash_ == null_hash);
}

} // namespace kth::domain::chain
