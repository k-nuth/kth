// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/transaction_entry.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

// Space optimization since valid sigops and size are never close to 32 bits.
inline
uint32_t cap(size_t value) {
    return domain_constrain<uint32_t>(value);
}

// TODO(legacy): implement size, sigops, and fees caching on domain::chain::transaction.
// This requires the full population of transaction.validation metadata.
transaction_entry::transaction_entry(transaction_const_ptr tx)
    : size_(cap(tx->serialized_size(domain::message::version::level::canonical)))
    , sigops_(cap(tx->signature_operations()))
    , fees_(tx->fees())
    , forks_(tx->validation.state->enabled_forks())
    , hash_(tx->hash())
    , marked_(false)
{}

// Create a search key.
transaction_entry::transaction_entry(hash_digest const& hash)
    : size_(0)
    , sigops_(0)
    , fees_(0)
    , forks_(0)
    , hash_(hash)
    , marked_(false)
{}

bool transaction_entry::is_anchor() const {
    return parents_.empty();
}

// Not valid if the entry is a search key.
uint64_t transaction_entry::fees() const {
    return fees_;
}

// Not valid if the entry is a search key.
uint32_t transaction_entry::forks() const {
    return forks_;
}

// Not valid if the entry is a search key.
size_t transaction_entry::sigops() const {
    return sigops_;
}

// Not valid if the entry is a search key.
size_t transaction_entry::size() const {
    return size_;
}

// Not valid if the entry is a search key.
hash_digest const& transaction_entry::hash() const {
    return hash_;
}

void transaction_entry::mark(bool value) {
    marked_ = value;
}

bool transaction_entry::is_marked() const {
    return marked_;
}

// Not valid if the entry is a search key.
transaction_entry::list const& transaction_entry::parents() const {
    return parents_;
}

// Not valid if the entry is a search key.
transaction_entry::list const& transaction_entry::children() const {
    return children_;
}

// This is not guarded against redundant entries.
void transaction_entry::add_parent(ptr parent) {
    parents_.push_back(parent);
}

// This is not guarded against redundant entries.
void transaction_entry::add_child(ptr child) {
    children_.push_back(child);
}

// This is guarded against missing entries.
void transaction_entry::remove_child(ptr child) {
    auto const it = find(children_.begin(), children_.end(), child);

    // TODO(legacy): this is a placeholder for subtree purge.
    // TODO(legacy): manage removal of bidirectional link add/remove.
    if (it != children_.end()) {
        children_.erase(it);
    }
}

std::ostream& operator<<(std::ostream& out, transaction_entry const& of) {
    out << encode_hash(of.hash_)
        << " " << of.parents_.size()
        << " " << of.children_.size();
    return out;
}

} // namespace kth::blockchain
