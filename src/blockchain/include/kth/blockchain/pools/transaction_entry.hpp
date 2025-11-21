// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_ENTRY_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_ENTRY_HPP

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <boost/functional/hash_fwd.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

/// This class is not thread safe.
struct KB_API transaction_entry {
    using ptr = std::shared_ptr<transaction_entry>;
    using list = std::vector<ptr>;

    /// Construct an entry for the pool.
    /// Never store an invalid transaction in the pool except for the cases of:
    /// double spend and input invalid due to forks change (sentinel forks).
    transaction_entry(transaction_const_ptr tx);

    /// Use this construction only as a search key.
    transaction_entry(hash_digest const& hash);

    /// An anchor tx binds a subgraph to the chain and is not itself mempool.
    bool is_anchor() const;

    /// The fees for the purpose of mempool reply and template optimization.
    uint64_t fees() const;

    /// The forks used for sigop count and validation of inputs.
    /// If the forks for the next block differ this must be recomputed.
    uint32_t forks() const;

    /// The sigops for the purpose of block limit computation.
    /// This is computed based on the specified forks as pertains to BIP16.
    size_t sigops() const;

    /// The size for the purpose of block limit computation.
    size_t size() const;

    /// The hash table entry identity.
    hash_digest const& hash() const;

    /// Used for DAG traversal.
    void mark(bool value);

    /// Used for DAG traversal.
    bool is_marked() const;

    /// The hash table entry's parent (prevout transaction) hashes.
    const list& parents() const;

    /// The hash table entry's child (input transaction) hashes.
    const list& children() const;

    /// Add transaction to the list of children of this transaction.
    void add_parent(ptr parent);

    /// Add transaction to the list of children of this transaction.
    void add_child(ptr child);

    /// Parents are never removed, as this invalidates the child.
    /// Removal of a child causing the subgraph connected to it to be pruned.
    void remove_child(ptr child);

    /// Serializer for debugging (temporary).
    friend
    std::ostream& operator<<(std::ostream& out, const transaction_entry& of);

private:
    // These are non-const to allow for default copy construction.
    uint64_t fees_;
    uint32_t forks_;
    uint32_t sigops_;
    uint32_t size_;
    hash_digest hash_;

    // Used in DAG search.
    bool marked_;

    // These do not affect the entry hash, so must be mutable.
    list parents_;
    list children_;
};

} // namespace kth::blockchain

#endif
