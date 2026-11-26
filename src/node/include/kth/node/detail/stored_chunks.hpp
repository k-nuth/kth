// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_STORED_CHUNKS_HPP
#define KTH_NODE_DETAIL_STORED_CHUNKS_HPP

// Not installed. The stored chunks the sync coordinator has heard of and has
// not yet counted: chunks are stored in any order, and the height the bodies
// are synced to moves only through consecutive ones.
//
// Owned by the coordinator and touched by nothing else.

#include <cstdint>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

struct stored_chunks {
    /// Count a chunk reported as stored and answer the height the bodies are
    /// synced to, which was `synced_to` before it.
    ///
    /// A chunk downloaded for a branch the chain, now at `chain_generation`,
    /// has since left is not counted: its heights name other blocks on the new
    /// branch, and counting it would move the height past bodies this node
    /// does not hold.
    [[nodiscard]]
    uint32_t stored(sync::chunk_validated const& chunk, uint64_t chain_generation, uint32_t synced_to) {
        if (chunk.from_abandoned_branch(chain_generation)) {
            return synced_to;
        }
        waiting_[chunk.start_height] = chunk.block_count;
        while (true) {
            auto const next = waiting_.find(synced_to + 1);
            if (next == waiting_.end()) {
                return synced_to;
            }
            synced_to = next->first + next->second - 1;
            waiting_.erase(next);
        }
    }

    /// Forget every chunk that reaches above `height`: the chain moved, and
    /// what was stored up there belongs to the branch it left.
    void forget_above(uint32_t height) {
        for (auto it = waiting_.begin(); it != waiting_.end();) {
            it = it->first + it->second - 1 > height ? waiting_.erase(it) : std::next(it);
        }
    }

    [[nodiscard]]
    size_t size() const {
        return waiting_.size();
    }

private:
    // First height of each chunk, and how many blocks it holds.
    boost::unordered_flat_map<uint32_t, uint32_t> waiting_;
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_STORED_CHUNKS_HPP
