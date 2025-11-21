// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_FORK_HPP
#define KTH_BLOCKCHAIN_FORK_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

using local_utxo_t = std::unordered_map<domain::chain::point, domain::chain::output const*>;
using local_utxo_set_t = std::vector<local_utxo_t>;

/// This class is not thread safe.
struct KB_API branch {
    using ptr = std::shared_ptr<branch>;
    using const_ptr = std::shared_ptr<const branch>;

    /// Establish a branch with the given parent height.
    branch(size_t height = 0);

    /// Set the height of the parent of this branch (fork point).
    void set_height(size_t height);

    /// Push the block onto the branch, true if successfully chains to parent.
    bool push_front(block_const_ptr block);

    /// The top block of the branch, if it exists.
    block_const_ptr top() const;

    /// The top block of the branch, if it exists.
    size_t top_height() const;

    /////// Populate unspent duplicate state in the context of the branch.
    ////void populate_duplicate(const domain::chain::transaction& tx) const;

    /// Populate prevout validation spend state in the context of the branch.
    void populate_spent(const domain::chain::output_point& outpoint) const;

    /// Populate prevout validation output state in the context of the branch.
    void populate_prevout(domain::chain::output_point const& outpoint) const;
    void populate_prevout(domain::chain::output_point const& outpoint, std::vector<std::unordered_map<domain::chain::point, domain::chain::output const*>> const& branch_utxo) const;

    /// The member block pointer list.
    block_const_ptr_list_const_ptr blocks() const;

    /// Determine if there are any blocks in the branch.
    bool empty() const;

    /// The number of blocks in the branch.
    size_t size() const;

    /// Summarize the work of the branch.
    uint256_t work() const;

    /// The hash of the parent of this branch (branch point).
    hash_digest hash() const;

    /// The height of the parent of this branch (branch point).
    size_t height() const;

    /// A checkpoint of the fork point, identical to { hash(), height() }.
    infrastructure::config::checkpoint fork_point() const;

    /// The bits of the block at the given height in the branch.
    bool get_bits(uint32_t& out_bits, size_t height) const;

    /// The bits of the block at the given height in the branch.
    bool get_version(uint32_t& out_version, size_t height) const;

    /// The bits of the block at the given height in the branch.
    bool get_timestamp(uint32_t& out_timestamp, size_t height) const;

    /// The hash of the block at the given height if it exists in the branch.
    bool get_block_hash(hash_digest& out_hash, size_t height) const;

protected:
    size_t index_of(size_t height) const;
    size_t height_at(size_t index) const;
    uint32_t median_time_past_at(size_t index) const;

private:
    size_t height_;

    /// The chain of blocks in the branch.
    block_const_ptr_list_ptr blocks_;
};

local_utxo_t create_local_utxo_set(domain::chain::block const& block);
local_utxo_set_t create_branch_utxo_set(branch::const_ptr const& branch);

} // namespace kth::blockchain

#endif
