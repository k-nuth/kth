// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_GOLOMB_SET_HPP
#define KTH_DOMAIN_CHAIN_GOLOMB_SET_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <span>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// Width of the Golomb-Rice remainder, in bits.
inline constexpr uint8_t golomb_set_p = 19;

// Modulus multiplier. Hashes are reduced into the range [0, N * M).
inline constexpr uint64_t golomb_set_m = 784931;

using golomb_key = std::array<uint8_t, 16>;

enum class golomb_set_error {
    // The item count does not fit in a uint32, which is the width of N.
    too_many_items,
    // The stream ended before N differences had been read.
    truncated
};

class KD_API golomb_set {
public:
    golomb_set() = default;

    // Builds the set of the distinct items under the key.
    static
    std::expected<golomb_set, golomb_set_error> build(golomb_key const& key, std::span<data_chunk const> items);

    // Parses the serialized form: CompactSize(N) and the bit stream. It checks that the stream holds N values.
    static
    std::expected<golomb_set, golomb_set_error> from_data(byte_span data);

    data_chunk to_data() const;
    uint32_t size() const;

    bool match(golomb_key const& key, byte_span item) const;
    bool match_any(golomb_key const& key, std::span<data_chunk const> items) const;

    friend bool operator==(golomb_set const&, golomb_set const&) = default;

private:
    // Number of distinct items encoded in bits_.
    uint32_t n_ = 0;
    // Bit stream only. The CompactSize count is written in front of this by to_data.
    data_chunk bits_;
};

} // namespace kth::domain::chain

#endif
