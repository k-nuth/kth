// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_SIGHASH_CACHE_HPP
#define KTH_DOMAIN_CHAIN_SIGHASH_CACHE_HPP

#include <optional>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::chain {

struct transaction;

/// The hashes a signature hash takes from the whole transaction.
///
/// A signature hash with the fork id commits to the outpoints, the sequences
/// and the outputs of the transaction (and, with SIGHASH_UTXOS, to the outputs
/// it spends) through one hash of each. They are the same for every input and
/// every signature, and each is a pass over all the inputs or outputs: hashed
/// again for every signature, a transaction of n inputs costs n * n.
///
/// This keeps them for one transaction: each is computed the first time a
/// signature hash asks for it. It is not part of the transaction. Whoever
/// verifies the inputs of a transaction creates one, hands it to each
/// verification, and drops it.
///
/// \pre `tx` outlives the cache and does not change while the cache is in use.
/// One thread at a time: a cache has one owner.
struct KD_API sighash_cache {
    explicit sighash_cache(transaction const& tx)
        : tx_(&tx)
    {}

    sighash_cache(sighash_cache const&) = delete;
    sighash_cache& operator=(sighash_cache const&) = delete;

    /// The transaction these hashes are of.
    [[nodiscard]]
    transaction const& tx() const { return *tx_; }

    [[nodiscard]]
    hash_digest const& inpoints_hash();

    [[nodiscard]]
    hash_digest const& sequences_hash();

    [[nodiscard]]
    hash_digest const& outputs_hash();

    /// \pre The prevouts of every input are in its validation cache.
    [[nodiscard]]
    hash_digest const& utxos_hash();

private:
    transaction const* tx_;
    std::optional<hash_digest> inpoints_;
    std::optional<hash_digest> sequences_;
    std::optional<hash_digest> outputs_;
    std::optional<hash_digest> utxos_;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_SIGHASH_CACHE_HPP
