// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/point.hpp>

#include <cstdint>
#include <utility>

#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::domain::chain {

constexpr auto store_point_size = std::tuple_size<point>::value;

// Serialization.
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

expect<void> point::to_data(byte_writer& writer, bool wire) const {
    if (auto r = writer.write_hash(hash_); ! r) {
        return r;
    }
    if (wire) {
        return writer.write_little_endian<uint32_t>(index_);
    }
    KTH_ASSERT(index_ == null_index || index_ < max_uint16);
    auto const compact = (index_ == null_index)
        ? max_uint16
        : static_cast<uint16_t>(index_);
    return writer.write_little_endian<uint16_t>(compact);
}

// Iterator.
//-----------------------------------------------------------------------------

point_iterator point::begin() const {
    return {*this};
}

point_iterator point::end() const {
    return {*this, unsigned(store_point_size)};
}

} // namespace kth::domain::chain
