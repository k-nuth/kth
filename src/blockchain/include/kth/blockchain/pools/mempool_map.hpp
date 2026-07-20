// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_MAP_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_MAP_HPP

#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

#include <kth/blockchain/pools/mempool_config.hpp>

#if defined(KTH_MEMPOOL_BACKEND_PARLAY)
#include <parlay_hash/unordered_map.h>
#else
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#endif

namespace kth::blockchain {

// Backend-native default hash: boost::hash for CFM, std::hash for ParlayHash.
// (We intentionally do NOT force std::hash on both.)
#if defined(KTH_MEMPOOL_BACKEND_PARLAY)
template <typename Key>
using mempool_default_hash = std::hash<Key>;
#else
template <typename Key>
using mempool_default_hash = boost::hash<Key>;
#endif

// Callback constraints for the read paths.
template <typename Fn, typename Value>
concept value_reader = std::invocable<Fn, Value const&>;

template <typename Fn, typename Key, typename Value>
concept entry_reader = std::invocable<Fn, Key const&, Value const&>;

// A thin, compile-time-selected concurrent hash map for the mempool. Exactly
// one backend is compiled in (see mempool_config.hpp and the `mempool_backend`
// conan option); there is no run-time switching. The surface is intentionally
// minimal and uniform across backends — richer per-backend returns (e.g. an
// erase count) are deliberately normalised to keep callers backend-agnostic:
//
//   try_insert(k, v)  -> insert if absent; true iff inserted (first-seen)
//   visit(k, fn)      -> if present, fn(Value const&) in place; true iff found
//   erase(k)          -> true iff an element was removed
//   for_each(fn)      -> fn(Key const&, Value const&) for every element
//   size()            -> element count
template <typename Key,
          typename Value,
          typename Hash = mempool_default_hash<Key>,
          typename Eq = std::equal_to<Key>>
    requires std::invocable<Hash, Key const&>
          && std::predicate<Eq, Key const&, Key const&>
struct mempool_map {

#if defined(KTH_MEMPOOL_BACKEND_PARLAY)
    using backend = parlay::parlay_unordered_map<Key, Value, Hash, Eq>;

    // ParlayHash default-constructs its Hash internally, so a supplied hasher
    // instance is ignored here (the parlay backend uses the hasher's fixed
    // default salt — it is measurement-only).
    explicit mempool_map(std::size_t initial_capacity = 0, Hash const& = Hash{})
        : map_(static_cast<long>(initial_capacity)) {}

    bool try_insert(Key const& key, Value value) {
        return ! map_.Insert(key, std::move(value)).has_value();
    }

    template <typename Fn>
        requires value_reader<Fn, Value>
    bool visit(Key const& key, Fn&& fn) const {
        return map_.Find(key, [&](auto const& entry) {
            fn(entry.second);   // Parlay passes the {key, value} entry.
            return 0;           // Find requires a return value; discarded.
        }).has_value();
    }

    bool erase(Key const& key) {
        return map_.Remove(key).has_value();
    }

    template <typename Fn>
        requires entry_reader<Fn, Key, Value>
    void for_each(Fn&& fn) const {
        for (auto const& [k, v] : map_) {
            fn(k, v);
        }
    }

    std::size_t size() const {
        return static_cast<std::size_t>(map_.size());
    }

private:
    // `mutable`: ParlayHash's Find/size are non-const, but the mempool's read
    // paths (visit/for_each/size) are const.
    mutable backend map_;

#else // KTH_MEMPOOL_BACKEND_CFM
    using backend = boost::concurrent_flat_map<Key, Value, Hash, Eq>;

    explicit mempool_map(std::size_t initial_capacity = 0, Hash const& hash = Hash{})
        : map_(initial_capacity, hash) {}

    bool try_insert(Key const& key, Value value) {
        return map_.emplace(key, std::move(value));
    }

    template <typename Fn>
        requires value_reader<Fn, Value>
    bool visit(Key const& key, Fn&& fn) const {
        return map_.cvisit(key, [&](auto const& kv) {
            fn(kv.second);
        }) != 0;
    }

    bool erase(Key const& key) {
        return map_.erase(key) != 0;
    }

    template <typename Fn>
        requires entry_reader<Fn, Key, Value>
    void for_each(Fn&& fn) const {
        map_.cvisit_all([&](auto const& kv) {
            fn(kv.first, kv.second);
        });
    }

    std::size_t size() const {
        return map_.size();
    }

private:
    // No `mutable`: boost::concurrent_flat_map's cvisit/cvisit_all/size are const.
    backend map_;
#endif
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_MAP_HPP
