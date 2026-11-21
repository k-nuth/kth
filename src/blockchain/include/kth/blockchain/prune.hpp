// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_PRUNE_HPP
#define KTH_BLOCKCHAIN_PRUNE_HPP

#include <cstddef>
#include <cstdint>
#include <expected>

#include <kth/blockchain/define.hpp>
#include <kth/database/block_store.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/database/header_index.hpp>

namespace kth::blockchain {

// =============================================================================
// Removing old block files
// =============================================================================
//
// A pruning node keeps the block and undo data of the last `keep_blocks` blocks
// it has connected and removes the block files below them. Files go whole, so
// the rule is stated per file: a file is removed when every block in it is more
// than `keep_blocks` below the built height.
//
// The built height is the one the UTXO set describes. Blocks above it are still
// to be applied and blocks within `keep_blocks` of it are what a reorganization
// may disconnect, so both stay.

/// What one pass removed.
struct prune_result {
    /// The lowest numbered block file the store holds after the pass.
    int32_t first_file{0};
    /// Block files removed by the pass.
    size_t files_removed{0};
    /// Blocks whose data went with those files.
    size_t blocks_forgotten{0};
};

/// The highest height whose data may be removed: `keep_blocks` blocks ending at
/// `built_height` stay. Zero when nothing may be removed, either because the
/// node keeps every block (`keep_blocks` zero) or because it has not built that
/// many yet.
[[nodiscard]] constexpr
uint32_t prune_height(uint32_t built_height, uint32_t keep_blocks) {
    if (keep_blocks == 0 || built_height <= keep_blocks) return 0;
    return built_height - keep_blocks;
}

/// The first height that must have undo data when the node starts.
///
/// Undo is captured above the trusted checkpoint, so that bound applies to
/// every node. A pruning node has also removed what lies at or below its
/// prune height, and no file it kept ends below that.
[[nodiscard]] constexpr
uint32_t first_height_with_undo(uint32_t trusted_height, uint32_t built_height,
                                uint32_t keep_blocks) {
    auto const above_checkpoint = trusted_height + 1;
    auto const above_pruned = prune_height(built_height, keep_blocks) + 1;
    return above_checkpoint > above_pruned ? above_checkpoint : above_pruned;
}

/// Remove the block files every block of which is at or below the prune height
/// of `built_height`.
///
/// The index says which file each block is in, forks included. A block above
/// the prune height keeps its file and every file after it, so what remains is
/// a single run of files up to the one being written. Before a file is removed
/// the blocks in it lose their position in the index, so a reader is told the
/// block is not there instead of being sent to a file that is gone.
///
/// Called by the task that applies blocks to the UTXO set, after the built
/// height it passes has been made durable.
[[nodiscard]] KB_API
std::expected<prune_result, database::result_code>
prune_block_files(database::header_index& index, database::block_store& store,
                  uint32_t built_height, uint32_t keep_blocks);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_PRUNE_HPP
