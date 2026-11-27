// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_STORED_CHUNKS_HPP
#define KTH_NODE_DETAIL_STORED_CHUNKS_HPP

// Not installed. The stored chunks the sync coordinator has heard of and has
// not yet counted: chunks are stored in any order, and the height the bodies
// are synced to moves only through chunks that reach it.
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
    ///
    /// Chunks can overlap. A body that arrives on its own, such as the block a
    /// compact block rebuilds, is stored as a chunk of one block, and a range
    /// asked of a peer before it came can hold the same height and the ones
    /// after it. Whichever of the two is stored second starts at or below the
    /// height the first one moved to. It counts for the blocks it holds above
    /// that height, and is dropped when it holds none.
    [[nodiscard]]
    uint32_t stored(sync::chunk_validated const& chunk, uint64_t chain_generation, uint32_t synced_to) {
        if (chunk.from_abandoned_branch(chain_generation)) {
            return synced_to;
        }
        // Two at one height: the longer holds every block the shorter does.
        auto const [slot, fresh] = waiting_.try_emplace(chunk.start_height, chunk.block_count);
        if ( ! fresh && chunk.block_count > slot->second) {
            slot->second = chunk.block_count;
        }
        // Through every chunk that reaches the height, until none does. One
        // that starts below the height reaches it as well as one that starts
        // right after it: left waiting, its blocks were never counted, the
        // height stayed below the end of its range, and that range read as in
        // flight for good.
        for (bool moved = true; moved;) {
            moved = false;
            for (auto it = waiting_.begin(); it != waiting_.end();) {
                if (it->first > synced_to + 1) {
                    ++it;
                    continue;
                }
                if (it->second != 0 && it->first + it->second - 1 > synced_to) {
                    synced_to = it->first + it->second - 1;
                    moved = true;
                }
                it = waiting_.erase(it);
            }
        }
        return synced_to;
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
