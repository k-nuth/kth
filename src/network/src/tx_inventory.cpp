// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/tx_inventory.hpp>

#include <algorithm>
#include <cmath>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::network {

// rolling_bloom_filter
// -----------------------------------------------------------------------------

rolling_bloom_filter::rolling_bloom_filter(uint32_t elements, double fp_rate) {
    auto const log_fp_rate = std::log(fp_rate);
    // The optimal number of hash functions is log(fp_rate) / log(0.5),
    // between 1 and 50.
    hash_functions_ = uint32_t(std::clamp(int(std::round(log_fp_rate / std::log(0.5))), 1, 50));
    // Between two and three generations of elements / 2 entries.
    entries_per_generation_ = (elements + 1) / 2;
    auto const max_elements = double(entries_per_generation_) * 3;
    auto const filter_bits = uint32_t(std::ceil(-1.0 * hash_functions_ * max_elements
        / std::log(1.0 - std::exp(log_fp_rate / hash_functions_))));
    // Two bits per position, in separate words: position P is bit (P & 63)
    // of words (P >> 6) * 2 and (P >> 6) * 2 + 1.
    words_ = size_t((filter_bits + 63) / 64) << 1;
    reset();
}

uint64_t rolling_bloom_filter::hash(uint32_t n, hash_digest const& value) const {
    return sip_hash_uint256_extra(k0_, k1_, value, n);
}

void rolling_bloom_filter::insert(hash_digest const& value) {
    if (data_.empty()) {
        data_.assign(words_, 0);
    }
    if (entries_this_generation_ == entries_per_generation_) {
        entries_this_generation_ = 0;
        ++generation_;
        if (generation_ == 4) {
            generation_ = 1;
        }
        auto const mask1 = uint64_t(0) - uint64_t(generation_ & 1);
        auto const mask2 = uint64_t(0) - uint64_t(generation_ >> 1);
        // Wipe the entries of the generation this one replaces.
        for (size_t p = 0; p < data_.size(); p += 2) {
            auto const p1 = data_[p];
            auto const p2 = data_[p + 1];
            auto const mask = (p1 ^ mask1) | (p2 ^ mask2);
            data_[p] = p1 & mask;
            data_[p + 1] = p2 & mask;
        }
    }
    ++entries_this_generation_;

    for (uint32_t n = 0; n < hash_functions_; ++n) {
        auto const h = hash(n, value);
        auto const bit = h & 63;
        auto const pos = size_t((h >> 32) % data_.size());
        data_[pos & ~size_t(1)] = (data_[pos & ~size_t(1)] & ~(uint64_t(1) << bit))
            | uint64_t(generation_ & 1) << bit;
        data_[pos | 1] = (data_[pos | 1] & ~(uint64_t(1) << bit))
            | uint64_t(generation_ >> 1) << bit;
    }
}

bool rolling_bloom_filter::contains(hash_digest const& value) const {
    if (data_.empty()) {
        return false;
    }
    for (uint32_t n = 0; n < hash_functions_; ++n) {
        auto const h = hash(n, value);
        auto const bit = h & 63;
        auto const pos = size_t((h >> 32) % data_.size());
        if ( ! (((data_[pos & ~size_t(1)] | data_[pos | 1]) >> bit) & 1)) {
            return false;
        }
    }
    return true;
}

void rolling_bloom_filter::reset() {
    k0_ = pseudo_random::generate<uint64_t>();
    k1_ = pseudo_random::generate<uint64_t>();
    entries_this_generation_ = 0;
    generation_ = 1;
    std::fill(data_.begin(), data_.end(), 0);
}

// tx_inventory
// -----------------------------------------------------------------------------

tx_inventory::tx_inventory()
    : known_(known_capacity, 0.000001)
    , announced_(announced_capacity, 0.000001)
{}

} // namespace kth::network
