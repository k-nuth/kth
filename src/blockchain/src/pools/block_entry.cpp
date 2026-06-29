// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_entry.hpp>

#include <algorithm>
#include <iostream>

#include <kth/blockchain/define.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

block_entry::block_entry(block_const_ptr block)
    : hash_(block->hash()), block_(block)
{}

// Create a search key.
block_entry::block_entry(hash_digest const& hash)
    : hash_(hash)
{}

block_const_ptr block_entry::block() const {
    return block_;
}

hash_digest const& block_entry::hash() const {
    return hash_;
}

// Not callable if the entry is a search key.
hash_digest block_entry::parent() const {
    KTH_ASSERT(block_);
    return block_->header().previous_block_hash();
}

// Not valid if the entry is a search key.
hash_list const& block_entry::children() const {
    ////KTH_ASSERT(block_);
    return children_;
}

// This is not guarded against redundant entries.
void block_entry::add_child(block_const_ptr child) const {
    children_.push_back(child->hash());
}

std::ostream& operator<<(std::ostream& out, block_entry const& of) {
    out << encode_hash(of.hash_)
        << " " << encode_hash(of.parent())
        << " " << of.children_.size();
    return out;
}

// For the purpose of bimap identity only the tx hash matters.
bool block_entry::operator==(block_entry const& other) const {
    return hash_ == other.hash_;
}

} // namespace kth::blockchain
