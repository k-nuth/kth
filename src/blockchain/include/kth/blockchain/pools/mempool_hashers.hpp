// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_HASHERS_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_HASHERS_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::blockchain {

// A plain, default-constructible outpoint identity (txid + output index) used
// as the spent_by_ map key. We do NOT use domain::chain::point: it is
// valid-by-construction (no default ctor), which the ParlayHash backend cannot
// accept as a key (it default-constructs its entries). This mirrors BCHN using
// a plain COutPoint as the mapNextTx key.
struct outpoint_key {
    hash_digest hash{};
    uint32_t index{};
    friend bool operator==(outpoint_key const&, outpoint_key const&) = default;

    // Ordering
    // Any total order does, and this one is only ever used to claim a
    // transaction's outpoints in the same sequence every caller would — see
    // mempool::add. It says nothing about the chain.
    friend auto operator<=>(outpoint_key const&, outpoint_key const&) = default;
};

// SipHash-based, salted hashers for the mempool keys — the Bitcoin Core / BCHN
// SaltedTxidHasher / SaltedOutpointHasher pattern. txids and outpoints already
// embed a uniform cryptographic hash, so no general-purpose mixing is needed;
// the per-node-random salt (k0,k1) makes bucket assignment unpredictable and
// defeats algorithmic-complexity (hash-flooding) DoS from adversarial P2P
// transactions.
//
// Production (boost::concurrent_flat_map) constructs these with a per-mempool
// random salt via the explicit ctor. The default ctor carries a FIXED salt,
// used only by backends that default-construct the hasher and cannot take an
// instance — ParlayHash, a measurement-only backend never exposed to
// adversaries.
// TODO(mempool): if ParlayHash ever ships to production, give it a real
// per-node salt (it would need to accept a hasher instance).

inline constexpr uint64_t mempool_hash_default_k0 = 0x9E3779B97F4A7C15ull;
inline constexpr uint64_t mempool_hash_default_k1 = 0xBF58476D1CE4E5B9ull;

struct salted_txid_hasher {
    using is_avalanching = std::true_type;  // SipHash output is well-distributed.

    uint64_t k0 = mempool_hash_default_k0;
    uint64_t k1 = mempool_hash_default_k1;

    salted_txid_hasher() = default;
    salted_txid_hasher(uint64_t k0_, uint64_t k1_) : k0(k0_), k1(k1_) {}

    std::size_t operator()(hash_digest const& txid) const {
        return static_cast<std::size_t>(sip_hash_uint256(k0, k1, txid));
    }
};

struct salted_outpoint_hasher {
    using is_avalanching = std::true_type;

    uint64_t k0 = mempool_hash_default_k0;
    uint64_t k1 = mempool_hash_default_k1;

    salted_outpoint_hasher() = default;
    salted_outpoint_hasher(uint64_t k0_, uint64_t k1_) : k0(k0_), k1(k1_) {}

    std::size_t operator()(outpoint_key const& outpoint) const {
        return static_cast<std::size_t>(
            sip_hash_uint256_extra(k0, k1, outpoint.hash, outpoint.index));
    }
};

// The short IDs of a compact block (BIP152), each with the position of its
// transaction in the block. A peer chooses the short IDs it sends, so it could
// choose them to collide in a table whose hash it can compute, and make every
// insertion walk the ones before it. The hash is keyed with a salt drawn for
// each table, which the peer cannot know: where its short IDs land is not its
// to choose, and there is nothing to bound.
struct salted_short_id_hasher {
    using is_avalanching = std::true_type;  // SipHash output is well-distributed.

    uint64_t k0 = pseudo_random::generate<uint64_t>();
    uint64_t k1 = pseudo_random::generate<uint64_t>();

    size_t operator()(uint64_t short_id) const {
        return size_t(sip_hasher(k0, k1).write(short_id).finalize());
    }
};

using short_id_slots = boost::unordered_flat_map<uint64_t, size_t, salted_short_id_hasher>;

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_HASHERS_HPP
