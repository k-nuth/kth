// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_ENTRY_HPP
#define KTH_BLOCKCHAIN_BLOCK_ENTRY_HPP

#include <iostream>

#include <boost/functional/hash_fwd.hpp>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

/// This class is not thread safe.
struct KB_API block_entry {
    ////typedef std::shared_ptr<transaction_entry> ptr;
    ////typedef std::vector<ptr> list;

    /// Construct an entry for the pool.
    /// Never store an invalid block in the pool.
    block_entry(block_const_ptr block);

    /// Use this construction only as a search key.
    block_entry(hash_digest const& hash);

    /// The block that the entry contains.
    block_const_ptr block() const;

    /// The hash table entry identity.
    hash_digest const& hash() const;

    /// The hash table entry's parent (preceding block) hash.
    hash_digest const& parent() const;

    /// The hash table entry's child (succeeding block) hashes.
    hash_list const& children() const;

    /// Add block to the list of children of this block.
    void add_child(block_const_ptr child) const;

    /// Serializer for debugging (temporary).
    friend
    std::ostream& operator<<(std::ostream& out, block_entry const& of);

    /// Operators.
    bool operator==(block_entry const& other) const;

private:
    // These are non-const to allow for default copy construction.
    hash_digest hash_;
    block_const_ptr block_;

    // TODO: could save some bytes here by holding the pointer in place of the
    // hash. This would allow navigation to the hash saving 24 bytes per child.
    // Children do not pertain to entry hash, so must be mutable.
    mutable hash_list children_;
};

} // namespace kth::blockchain

// Standard (boost) hash.
//-----------------------------------------------------------------------------

namespace boost {

// Extend boost namespace with our block_const_ptr hash function.
template <>
struct hash<kth::blockchain::block_entry> {
    size_t operator()(kth::blockchain::block_entry const& entry) const {
        return boost::hash<kth::hash_digest>()(entry.hash());
    }
};

} // namespace boost

#endif
