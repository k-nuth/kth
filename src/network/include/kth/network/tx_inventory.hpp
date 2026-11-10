// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_TX_INVENTORY_HPP
#define KTH_NETWORK_TX_INVENTORY_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

/// BCHN CRollingBloomFilter: a filter of roughly the last `elements` hashes
/// inserted, with false positives at `fp_rate`. It keeps three generations of
/// elements / 2 entries and forgets the oldest as a new one starts. Keyed by a
/// random SipHash key, drawn again on reset(). Allocated on the first insert.
class KN_API rolling_bloom_filter {
public:
    rolling_bloom_filter(uint32_t elements, double fp_rate);

    void insert(hash_digest const& hash);

    [[nodiscard]]
    bool contains(hash_digest const& hash) const;

    void reset();

private:
    [[nodiscard]]
    uint64_t hash(uint32_t n, hash_digest const& value) const;

    uint32_t hash_functions_;
    uint32_t entries_per_generation_;
    uint32_t entries_this_generation_{0};
    uint32_t generation_{1};
    size_t words_;
    uint64_t k0_{0};
    uint64_t k1_{0};
    std::vector<uint64_t> data_;
};

/// What a peer told the node about the transactions it wants announced: the
/// ones it knows already (BCHN filterInventoryKnown) and its fee filter
/// (BIP133, BCHN minFeeFilter). And, apart from those, the ones the node
/// announced to it: only these are served when it asks (BCHN serves from
/// mapRelay, what it relayed). What the peer says it knows never makes one
/// servable: an `inv` of a pooled transaction not yet announced would give it
/// away before its turn, and tell the peer it is pooled.
///
/// Owned by the peer's session and touched only on the session's strand: the
/// read loop records what the peer announces, sends and asks to filter, and
/// the peer's service records what it announces and reads all of it. Both run
/// on that strand (p2p_node::run_peer_protocols); nothing else touches it.
class KN_API tx_inventory {
public:
    /// BCHN sizes filterInventoryKnown for 50,000 entries at one in a million.
    static constexpr uint32_t known_capacity = 50'000;

    tx_inventory();

    /// What a peer asks for follows the announcement within moments.
    /// Bitcoin Core keeps the same check for this many
    /// (m_recently_announced_invs, INVENTORY_MAX_RECENT_RELAY), at one in a
    /// million: about 19 KB a peer, where the known filter takes 270 KB.
    static constexpr uint32_t announced_capacity = 3'500;

    void add_known(hash_digest const& txid) { known_.insert(txid); }

    [[nodiscard]]
    bool known(hash_digest const& txid) const { return known_.contains(txid); }

    /// Announced to the peer, so known to it as well.
    void add_announced(hash_digest const& txid) {
        known_.insert(txid);
        announced_.insert(txid);
    }

    [[nodiscard]]
    bool announced(hash_digest const& txid) const { return announced_.contains(txid); }

    /// The peer's minimum fee per 1000 bytes; zero filters nothing.
    [[nodiscard]]
    uint64_t fee_filter() const { return fee_filter_; }

    void set_fee_filter(uint64_t fee_per_kb) { fee_filter_ = fee_per_kb; }

    /// The peer's `mempool` was answered with the pool as it was when the
    /// last admission was `through`: what was pooled then may be served
    /// to it as well (BCHN timeLastMempoolReq).
    void mempool_answered(uint64_t through) { mempool_answered_ = through; }

    /// The last admission the pool held when the peer's `mempool` was last
    /// answered; nullopt when it never was.
    [[nodiscard]]
    std::optional<uint64_t> mempool_answered() const { return mempool_answered_; }

private:
    rolling_bloom_filter known_;
    rolling_bloom_filter announced_;
    uint64_t fee_filter_{0};
    std::optional<uint64_t> mempool_answered_;
};

/// What a peer's service hears when a transaction is admitted: the last
/// admission's id, and when the node's inbound peers are next due an
/// announcement (BCHN gives them one schedule, so they all hear of a
/// transaction at once).
struct tx_admissions {
    uint64_t last{0};
    std::chrono::steady_clock::time_point inbound_due{};
};

} // namespace kth::network

#endif // KTH_NETWORK_TX_INVENTORY_HPP
