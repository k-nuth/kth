// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_POINT_HPP
#define KTH_DOMAIN_CHAIN_POINT_HPP

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>

#include <kth/domain/chain/point_iterator.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

struct KD_API point {
    /// This is a sentinel used in .index to indicate no output, e.g. coinbase.
    /// This value is serialized and defined by consensus, not implementation.
    static constexpr
    uint32_t null_index = no_previous_output;

    using list = std::vector<point>;
    using opt = std::optional<point>;
    using indexes = std::vector<uint32_t>;

    // Constructors.
    //-------------------------------------------------------------------------

    constexpr point(hash_digest const& hash, uint32_t index)
        : index_(index), hash_(hash)
    {}

    // Operators.
    //-------------------------------------------------------------------------

    // Member order (index_, then hash_) is deliberate: it makes the defaulted
    // `<=>` compare index first — index comparisons are cheaper than 32-byte
    // hashes, and this matches the pre-refactor hand-written `operator<`
    // ("arbitrary order to support set uniqueness").
    friend constexpr auto operator<=>(point const&, point const&) = default;

    // Serialization.
    //-------------------------------------------------------------------------

    static
    expect<point> from_data(byte_reader& reader, bool wire = true);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, bool wire) const;

    [[nodiscard]]
    static constexpr size_t satoshi_fixed_size() {
        return hash_size + sizeof(index_);
    }

    [[nodiscard]]
    constexpr size_t serialized_size(bool wire = true) const {
        return wire ? point::satoshi_fixed_size() : (hash_size + sizeof(uint16_t));
    }

    /// Returns a null point (coinbase input reference).
    [[nodiscard]]
    static constexpr point null() noexcept {
        return point{null_hash, null_index};
    }

    // Iteration (limited to store serialization).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    point_iterator begin() const;

    [[nodiscard]]
    point_iterator end() const;

    // Properties (accessors, cache).
    //-------------------------------------------------------------------------

    // deprecated (unsafe)
    constexpr hash_digest& hash() {
        return hash_;
    }

    [[nodiscard]]
    constexpr hash_digest const& hash() const {
        return hash_;
    }

    constexpr void set_hash(hash_digest const& value) {
        hash_ = value;
    }

    [[nodiscard]]
    constexpr uint32_t index() const {
        return index_;
    }

    constexpr void set_index(uint32_t value) {
        index_ = value;
    }

    // Utilities.
    //-------------------------------------------------------------------------

    /// This is for client-server, not related to consensus or p2p networking.
    [[nodiscard]]
    constexpr uint64_t checksum() const {
        // Reserve 49 bits for the tx hash and 15 bits (32768) for the input index.
        constexpr uint64_t mask = 0xffffffffffff8000;

        // Use an offset to the middle of the hash to avoid coincidental mining
        // of values into the front or back of tx hash (not a security feature).
        // Use most possible bits of tx hash to make intentional collision hard.
        auto const tx = from_little_endian_unsafe<uint64_t>(std::span{hash_}.subspan(12));
        auto const index = uint64_t(index_);

        auto const tx_upper_49_bits = tx & mask;
        auto const index_lower_15_bits = index & ~mask;
        return tx_upper_49_bits | index_lower_15_bits;
    }

    // Validation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    constexpr bool is_null() const {
        return index_ == null_index && hash_ == null_hash;
    }

private:
    uint32_t index_;
    hash_digest hash_;
};

using point_opt = point::opt;

} // namespace kth::domain::chain

// Standard hash.
//-----------------------------------------------------------------------------

namespace std {

// Extend std namespace with our hash wrapper (database key, not checksum).
template <>
struct hash<kth::domain::chain::point> {
    size_t operator()(const kth::domain::chain::point& point) const {
        size_t seed = 0;
        boost::hash_combine(seed, point.hash());
        boost::hash_combine(seed, point.index());
        return seed;
    }
};

// Extend std namespace with the non-wire size of point (database key size).
template <>
struct tuple_size<kth::domain::chain::point> {
    static
    auto const value = std::tuple_size<kth::hash_digest>::value + sizeof(uint16_t);

    operator std::size_t() const {
        return value;
    }
};

} // namespace std

//#include <kth/domain/concepts_undef.hpp>

#endif
