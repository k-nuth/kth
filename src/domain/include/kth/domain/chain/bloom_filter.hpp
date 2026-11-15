// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BLOOM_FILTER_HPP
#define KTH_DOMAIN_CHAIN_BLOOM_FILTER_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// The filter a light wallet loads into a peer with `filterload` (BIP37): a
// bit field, a number of hash functions, a tweak the wallet picks, and flags
// that say what the node adds to the filter as transactions match.
//
// It lives for one connection and is the wallet's own, so a node keeps
// nothing for it.
struct KD_API bloom_filter {
    // The largest filter a peer may load, in bytes, and the most hash
    // functions it may ask for.
    static constexpr size_t max_size = 36000;
    static constexpr uint32_t max_hash_functions = 50;
    // The largest element a `filteradd` may carry: the largest script push.
    static constexpr size_t max_element_size = 520;

    // The low two bits of the flags: whether the outpoint of a matched output
    // is added to the filter, so the transaction that spends it matches too.
    static constexpr uint8_t update_none = 0;
    static constexpr uint8_t update_all = 1;
    // Only for outputs that pay to a public key or to a bare multisig.
    static constexpr uint8_t update_p2pubkey_only = 2;
    static constexpr uint8_t update_mask = 3;

    bloom_filter() = default;
    bloom_filter(data_chunk bits, uint32_t hash_functions, uint32_t tweak, uint8_t flags);

    // An empty filter sized for a number of elements at a false positive
    // rate, as wallets size theirs.
    [[nodiscard]]
    static bloom_filter sized(size_t elements, double false_positive_rate, uint32_t tweak, uint8_t flags);

    // Whether a peer may load it.
    [[nodiscard]] bool within_limits() const;

    void insert(byte_span data);
    void insert_outpoint(point const& outpoint);
    // A filter of no bytes holds everything; so does one of no hash functions.
    [[nodiscard]] bool contains(byte_span data) const;
    [[nodiscard]] bool contains_outpoint(point const& outpoint) const;

    // Whether the transaction's hash or a data push of one of its output
    // scripts is in the filter. The outpoint of each matched output is added
    // as the flags say.
    [[nodiscard]] bool match_and_insert_outputs(transaction const& tx);

    // Whether an outpoint one of its inputs spends, or a data push of one of
    // its input scripts, is in the filter.
    [[nodiscard]] bool match_inputs(transaction const& tx) const;

    // Both, for one transaction on its own. For a block the outputs of every
    // transaction go first: a transaction can stand before the one it spends.
    [[nodiscard]] bool is_relevant_and_update(transaction const& tx);

    [[nodiscard]] data_chunk const& bits() const noexcept { return bits_; }
    [[nodiscard]] uint32_t hash_functions() const noexcept { return hash_functions_; }
    [[nodiscard]] uint32_t tweak() const noexcept { return tweak_; }
    [[nodiscard]] uint8_t flags() const noexcept { return flags_; }

    [[nodiscard]]
    friend bool operator==(bloom_filter const&, bloom_filter const&) = default;

private:
    [[nodiscard]] size_t bit_of(uint32_t function, byte_span data) const;

    data_chunk bits_;
    uint32_t hash_functions_ = 0;
    uint32_t tweak_ = 0;
    uint8_t flags_ = update_none;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_BLOOM_FILTER_HPP
