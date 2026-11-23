// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/prune.hpp>

#include <limits>

namespace kth::blockchain {

using database::header_index;
using database::header_status;

std::expected<prune_result, database::result_code>
prune_block_files(header_index& index, database::block_store& store,
                  uint32_t built_height, uint32_t keep_blocks) {
    prune_result result;
    result.first_file = store.first_file();

    auto const limit = prune_height(built_height, keep_blocks);
    if (limit == 0) {
        return result;
    }

    // The lowest file holding a block that has to stay.
    auto const entries = static_cast<header_index::index_t>(index.size());
    constexpr auto none = std::numeric_limits<int32_t>::max();
    int32_t keep_from = none;
    for (header_index::index_t idx = 0; idx < entries; ++idx) {
        auto const file = int32_t(index.get_file_number(idx));
        if (file < 0 || file >= keep_from) continue;
        auto const height = index.get_height(idx);
        if (height > 0 && uint32_t(height) > limit) {
            keep_from = file;
        }
    }

    // No block above the prune height has data: nothing says where the files
    // to keep begin, so none is removed.
    if (keep_from == none || keep_from <= result.first_file) {
        return result;
    }

    // Forgotten before the files go: from here a reader finds no position,
    // which is "not here", instead of one that names a file about to vanish.
    for (header_index::index_t idx = 0; idx < entries; ++idx) {
        auto const file = int32_t(index.get_file_number(idx));
        if (file < 0 || file >= keep_from) continue;
        index.set_block_pos(idx, -1, 0);
        index.set_undo_pos(idx, 0);
        index.remove_status(idx, header_status::have_data | header_status::have_undo);
        ++result.blocks_forgotten;
    }

    auto const removed = store.remove_files_below(keep_from);
    if ( ! removed) {
        return std::unexpected(removed.error());
    }
    result.files_removed = *removed;
    result.first_file = store.first_file();
    return result;
}

} // namespace kth::blockchain
