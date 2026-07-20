// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_pool.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>

// Atomicity is not required for these operations as each validation call is
// sequenced. Locking is performed only to guard concurrent filtering.

namespace kth::blockchain {

using namespace boost;

block_pool::block_pool(size_t maximum_depth, block_validation_store& block_validations)
    : maximum_depth_(maximum_depth == 0 ? max_size_t : maximum_depth)
    , block_validations_(block_validations)
{}

size_t block_pool::size() const {
    return blocks_.size();
}

void block_pool::add(block_const_ptr valid_block) {
    // The block must be successfully validated.
    ////KTH_ASSERT( ! block->validation.error);
    block_entry entry{ valid_block };

    // Height previously came from header().validation.height; the header
    // is now a pure value type, so we read it from the block's chain_state
    // which is populated by the validator before the block reaches the pool.
    // Fall back to 0 to preserve the legacy semantics when an unvalidated
    // block (no chain_state) is added — e.g. dedup-only test paths.
    size_t height = 0;
    block_validations_.visit(valid_block->hash(), [&](auto const& bv){
        if (bv.state) {
            height = static_cast<size_t>(bv.state->height());
        }
    });
    auto const& left = blocks_.left;

    // Caller ensure the entry does not exist by using get_path, but
    // insert rejects the block if there is an entry of the same hash.
    ////KTH_ASSERT(left.find(entry) == left.end());

    // Add a back pointer from the parent for clearing the path later.
    block_entry const parent{ valid_block->header().previous_block_hash() };
    auto const it = left.find(parent);

    if (it != left.end()) {
        height = 0;
        it->first.add_child(valid_block);
    }

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);
    blocks_.insert({ std::move(entry), height });
    ///////////////////////////////////////////////////////////////////////////
}

void block_pool::add(block_const_ptr_list_const_ptr valid_blocks) {
    auto const insert = [&](block_const_ptr const& block) { add(block); };
    std::for_each(valid_blocks->begin(), valid_blocks->end(), insert);
}

// The pool is a forest connected to the chain at the roots of each tree.
// We delete only roots, pulling the tree "down" as we go based on expiration
// or acceptance. So there is never internal removal of a node.
void block_pool::remove(block_const_ptr_list_const_ptr accepted_blocks) {
    hash_list child_hashes;
    auto saver = [&](hash_digest const& hash){ child_hashes.push_back(hash); };
    auto& left = blocks_.left;

    for (auto block: *accepted_blocks) {
        // The block is now on the accepted chain; its validation state is no
        // longer needed. Evict regardless of pool membership (a block accepted
        // straight onto the main chain has a store entry but no pool entry).
        block_validations_.erase(block->hash());

        auto it = left.find(block_entry{ block->hash() });

        if (it == left.end()) continue;

        // Copy hashes of all children of nodes we delete.
        auto const& children = it->first.children();
        std::for_each(children.begin(), children.end(), saver);

        ///////////////////////////////////////////////////////////////////////
        // Critical Section
        unique_lock lock(mutex_);
        left.erase(it);
        ///////////////////////////////////////////////////////////////////////
    }

    // Move all children that we have orphaned to the root (give them height).
    for (auto child: child_hashes) {
        auto it = left.find(block_entry{ child });

        // Except for sub-branches all children should have been deleted above.
        if (it == left.end()) continue;

        // Copy the entry so that it can be deleted and replanted with height.
        auto const copy = it->first;
        domain::chain::chain_state::ptr state;
        block_validations_.visit(copy.block()->hash(), [&](auto const& bv){ state = bv.state; });
        KTH_ASSERT(state);
        auto const height = static_cast<size_t>(state->height());
        KTH_ASSERT(it->second == 0);

        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        unique_lock lock(mutex_);
        left.erase(it);
        blocks_.insert({ copy, height });
        ///////////////////////////////////////////////////////////////////////
    }
}

// protected
void block_pool::prune(hash_list const& hashes, size_t minimum_height) {
    hash_list child_hashes;
    auto saver = [&](hash_digest const& hash){ child_hashes.push_back(hash); };
    auto& left = blocks_.left;

    for (auto const& hash: hashes) {
        auto const it = left.find(block_entry{ hash });
        KTH_ASSERT(it != left.end());

        domain::chain::chain_state::ptr state;
        block_validations_.visit(it->first.block()->hash(), [&](auto const& bv){ state = bv.state; });
        KTH_ASSERT(state);
        auto const height = static_cast<size_t>(state->height());

        // Delete all roots and expired non-roots and recurse their children.
        if (it->second != 0 || height < minimum_height) {
            // delete -- the block leaves the pool, so drop its validation state.
            block_validations_.erase(hash);

            auto const& children = it->first.children();
            std::for_each(children.begin(), children.end(), saver);

            ///////////////////////////////////////////////////////////////////
            // Critical Section
            unique_lock lock(mutex_);
            left.erase(it);
            ///////////////////////////////////////////////////////////////////

            continue;
        }

        // Copy the entry so that it can be deleted and replanted with height.
        auto const copy = it->first;

        // Critical Section
        ///////////////////////////////////////////////////////////////////////
        unique_lock lock(mutex_);
        left.erase(it);
        blocks_.insert({ copy, height });
        ///////////////////////////////////////////////////////////////////////
    }

    // Recurse the children to span the tree.
    if ( ! child_hashes.empty()) {
        prune(child_hashes, minimum_height);
    }
}

void block_pool::prune(size_t top_height) {
    hash_list hashes;
    auto const minimum_height = floor_subtract(top_height, maximum_depth_);

    // The right view is ordered by height, so everything from the first entry
    // at or above the minimum is out of range.
    auto const end = blocks_.right.lower_bound(minimum_height);

    for (auto it = blocks_.right.begin(); it != end; ++it) {
        if (it->first != 0) {
            hashes.push_back(it->second.hash());
        }
    }

    // Get outside of the hash table iterator before deleting.
    if ( ! hashes.empty()) {
        prune(hashes, minimum_height);
    }
}

void block_pool::filter(get_data_ptr message) const {
    auto const& left = blocks_.left;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_shared();

    message->erase_if([&left](auto const& inv) {
        return inv.is_block_type() &&
               left.find(block_entry{ inv.hash() }) != left.end();
    });

    mutex_.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////
}

// protected
bool block_pool::exists(block_const_ptr candidate_block) const {
    // The block must not yet be successfully validated.
    ////KTH_ASSERT(candidate_block->validation.error);
    auto const& left = blocks_.left;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);
    return left.find(block_entry{ candidate_block }) != left.end();
    ///////////////////////////////////////////////////////////////////////////
}

// protected
block_const_ptr block_pool::parent(block_const_ptr block) const {
    // The block may be validated (pool) or not (new).
    block_entry const parent_entry{ block->header().previous_block_hash() };
    auto const& left = blocks_.left;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);
    auto const parent = left.find(parent_entry);
    return parent == left.end() ? nullptr : parent->first.block();
    ///////////////////////////////////////////////////////////////////////////
}

branch::ptr block_pool::get_path(block_const_ptr block) const {
    ////log_content();
    auto const trace = std::make_shared<branch>(0, block_validations_);

    if (exists(block)) return trace;

    while (block) {
        trace->push_front(block);
        block = parent(block);
    }

    return trace;
}

////// private
////void block_pool::log_content() const
////{
////    spdlog::info("[blockchain] pool: ");
// void block_pool::trace() const {
//     // Dump in hash order with height suffix (roots have height).
//     for (auto const& entry: blocks_.left) {
//         spdlog::info("[blockchain] {} {}", entry.first, entry.second);
//     }
// }

} // namespace kth::blockchain