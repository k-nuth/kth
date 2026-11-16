// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_SPENT_OUTPUTS_HPP
#define KTH_BLOCKCHAIN_SPENT_OUTPUTS_HPP

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <vector>

#include <kth/blockchain/define.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

// The outputs a block spends are not in the block. The node keeps them in the
// undo record it writes when it connects a block, which holds every output the
// block spent that existed before it. What follows reads them back in the
// order of the block's inputs, and nothing is stored for it.

// Reads the output an undo entry stands for. In full mode the entry holds the
// output; in reference mode it points at the transaction in the block files.
using undo_output_reader = std::function<
    std::expected<domain::chain::output, database::result_code>(database::spent_output const&)>;

// The output held by a full-mode undo entry: the entry is a stored UTXO entry,
// and is read as one.
[[nodiscard]]
KB_API
std::expected<domain::chain::output, database::result_code>
output_of_stored_entry(std::span<uint8_t const> value);

// The outputs a block spends, one for each input of each transaction after the
// coinbase, in the order of the inputs, and the height each was created at.
struct KB_API spent_output_list {
    std::vector<domain::chain::output> outputs;
    std::vector<uint32_t> heights;
};

// An undo record is not in the order of the inputs and does not cover every
// input: its entries are found by outpoint, and an output created and spent in
// the same block has no entry, so it is taken from the block, created at
// `height`, the block's own. The transaction that creates it can stand after
// the one that spends it, as the transactions of a block are ordered by their
// hashes.
//
// key_not_found when an input spends an output that is in neither. An entry no
// input names is left alone: a block that re-creates an earlier coinbase keeps
// the entry it displaced in its record.
[[nodiscard]]
KB_API
std::expected<spent_output_list, database::result_code>
spent_outputs_with_heights(domain::chain::block const& blk, uint32_t height,
                           database::block_undo const& undo, undo_output_reader const& read);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_SPENT_OUTPUTS_HPP
