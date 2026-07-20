// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_VALIDATION_STORE_HPP
#define KTH_BLOCKCHAIN_VALIDATE_VALIDATION_STORE_HPP

#include <concepts>
#include <utility>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/infrastructure/hash_define.hpp>

namespace kth::blockchain {

/// Validator-owned side store of transient per-object validation state, keyed
/// by object hash. Generic over the value type (block_validation,
/// transaction_validation, ...); the value carries no consensus/wire identity,
/// only the validator's working annotations for one object. It must be
/// default-constructible because the store materializes a default entry the
/// first time an object is written.
///
/// The surface is visitation-only: mutate/visit apply a callable to the entry
/// and never hand out a reference or pointer to it. No internal
/// synchronization -- every access happens inside the organizers' serialized
/// organize() path (block and transaction organizers share one prioritized
/// mutex held across their co_awaits, so they are mutually exclusive).
template <std::default_initializable Validation>
struct validation_store {
    /// Apply `fn` (taking `Validation&`) to the entry for `hash`, creating a
    /// default entry first if none exists. Returns true if a new entry was
    /// created, false if an existing one was mutated. For writers.
    template <typename Fn>
    bool mutate(hash_digest const& hash, Fn&& fn) {
        auto const [it, created] = map_.try_emplace(hash);
        std::forward<Fn>(fn)(it->second);
        return created;
    }

    /// If an entry for `hash` exists, apply `fn` (taking `Validation const&`)
    /// to it and return true; otherwise return false. For readers.
    template <typename Fn>
    bool visit(hash_digest const& hash, Fn&& fn) const {
        auto const it = map_.find(hash);
        if (it == map_.end()) {
            return false;
        }
        std::forward<Fn>(fn)(it->second);
        return true;
    }

    /// Drop the entry for `hash`. Returns the number of entries removed (0 or
    /// 1), i.e. whatever the underlying map's erase returns.
    std::size_t erase(hash_digest const& hash) {
        return map_.erase(hash);
    }

private:
    boost::unordered_flat_map<hash_digest, Validation> map_;
};

} // namespace kth::blockchain

#endif
