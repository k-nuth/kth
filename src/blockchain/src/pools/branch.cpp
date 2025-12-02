// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/branch.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <numeric>
#include <utility>

#include <kth/blockchain/define.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/utility/limits.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::config;

local_utxo_t create_local_utxo_set(domain::chain::block const& block) {
    //TODO(fernando): confirm if there is a validation to check that the coinbase tx is not spend, before this.
    //                we avoid to insert the coinbase in the local utxo set

    local_utxo_t res;
    res.reserve(block.transactions().size());
    for (auto const& tx : block.transactions()) {
        auto const& outputs = tx.outputs();
        for (uint32_t idx = 0; idx < outputs.size(); ++idx) {
            auto const& output = outputs[idx];
            // std::println("{}", "create_local_utxo_set - output: {" << encode_hash(tx.hash()) << " - " << idx << "}");
            res.emplace(output_point{tx.hash(), idx}, std::addressof(output));
        }
    }
    return res;
}

local_utxo_set_t create_branch_utxo_set(branch::const_ptr const& branch) {
    auto blocks = *branch->blocks();

    local_utxo_set_t res;
    res.reserve(branch->size());

    for (auto const& block : blocks) {
        // std::println("{}", "create_branch_utxo_set - block: {" << encode_hash(block->hash()) << "}");
        res.push_back(create_local_utxo_set(*block));
    }

    return res;
}



// This will be eliminated once weak block headers are moved to the store.
branch::branch(size_t height)
    : height_(height)
    , blocks_(std::make_shared<block_const_ptr_list>())
{}

void branch::set_height(size_t height) {
    height_ = height;
}

// Front is the top of the chain plus one, back is the top of the branch.
bool branch::push_front(block_const_ptr block) {
    auto const linked = [this](block_const_ptr block) {
        auto const& front = blocks_->front()->header();
        return front.previous_block_hash() == block->hash();
    };

    if (empty() || linked(block)) {
        blocks_->insert(blocks_->begin(), block);
        return true;
    }

    return false;
}

block_const_ptr branch::top() const {
    return empty() ? nullptr : blocks_->back();
}

size_t branch::top_height() const {
    return height() + size();
}

block_const_ptr_list_const_ptr branch::blocks() const {
    return blocks_;
}

bool branch::empty() const {
    return blocks_->empty();
}

size_t branch::size() const {
    return blocks_->size();
}

size_t branch::height() const {
    return height_;
}

hash_digest branch::hash() const {
    return empty() ? null_hash : blocks_->front()->header().previous_block_hash();
}

infrastructure::config::checkpoint branch::fork_point() const {
    return {hash(), height()};
}

// private
size_t branch::index_of(size_t height) const {
    // The member height_ is the height of the fork point, not the first block.
    return *safe_subtract(*safe_subtract(height, height_), size_t(1));
}

// private
size_t branch::height_at(size_t index) const {
    // The height of the blockchain branch point plus zero-based index.
    return *safe_add(*safe_add(index, height_), size_t(1));
}

// private
uint32_t branch::median_time_past_at(size_t index) const {
    KTH_ASSERT(index < size());
    return (*blocks_)[index]->header().validation.median_time_past;
}

// TODO(legacy): absorb into the main chain for speed and code consolidation.
// The branch work check is both a consensus check and denial of service
// protection. It is necessary here that total claimed work exceeds that of the
// competing chain segment (consensus), and that the work has actually been
// expended (denial of service protection). The latter ensures we don't query
// the chain for total segment work path the branch competetiveness. Once work
// is proven sufficient the blocks are validated, requiring each to have the
// work required by the header accept check. It is possible that a longer chain
// of lower work blocks could meet both above criteria. However this requires
// the same amount of work as a shorter segment, so an attacker gains no
// advantage from that option, and it will be caught in validation.
uint256_t branch::work() const {
    uint256_t total;
    // Not using accumulator here avoids repeated copying of uint256 object.
    for (auto block : *blocks_) {
        total += block->proof();
    }
    return total;
}

// TODO(legacy): convert to a direct block pool query when the branch goes away.
// BUGBUG: this does not differentiate between spent and unspent txs.
// Spent transactions could exist in the pool due to other txs in the same or
// later pool blocks. So this is disabled in favor of "allowed collisions".
// Otherwise it could reject a spent duplicate. Given that collisions must be
// rejected at least prior to the BIP34 checkpoint this is technically a
// consensus break which would only apply to a reorg at height less than BIP34.
////void branch::populate_duplicate(const domain::chain::transaction& tx) const
////{
////    auto const outer = [&tx](size_t total, block_const_ptr block)
////    {
////        auto const hashes = [&tx](transaction const& block_tx)
////        {
////            return block_tx.hash() == tx.hash();
////        };
////
////        auto const& txs = block->transactions();
////        return total + std::count_if(txs.begin(), txs.end(), hashes);
////    };
////
////    // Counting all is easier than excluding self and terminating early.
////    auto const count = std::accumulate(blocks_->begin(), blocks_->end(),
////        size_t(0), outer);
////
////    KTH_ASSERT(count > 0);
////    tx.validation.duplicate = count > 1u;
////}

// TODO(legacy): convert to a direct block pool query when the branch goes away.
void branch::populate_spent(output_point const& outpoint) const {
    auto& prevout = outpoint.validation;

    // Assuming (1) block.check() validates against internal double spends
    // and (2) the outpoint is of the top block, there is no need to consider
    // the top block here. Under these assumptions spends in the top block
    // could only be double spent by a spend in a preceding block. Excluding
    // the top block requires that we consider 1 collision spent (vs. > 1).
    if (size() < 2u) {
        prevout.spent = false;
        prevout.confirmed = false;
        return;
    }

    // TODO(legacy): use hash table storage of block's inputs for block pool entries.
    auto const blocks = [&outpoint](block_const_ptr block) {
        auto const transactions = [&outpoint](transaction const& tx) {
            auto const prevout_match = [&outpoint](input const& input) {
                return input.previous_output() == outpoint;
            };

            auto const& ins = tx.inputs();
            return std::any_of(ins.begin(), ins.end(), prevout_match);
        };

        auto const& txs = block->transactions();
        KTH_ASSERT_MSG( ! txs.empty(), "empty block in branch");
        return std::any_of(txs.begin() + 1, txs.end(), transactions);
    };

    auto spent = std::any_of(blocks_->begin(), blocks_->end() - 1, blocks);
    prevout.spent = spent;
    prevout.confirmed = prevout.spent;
}

// TODO(legacy): absorb into the main chain for speed and code consolidation.
void branch::populate_prevout(output_point const& outpoint) const {
    auto& prevout = outpoint.validation;

    // In case this input is a coinbase or the prevout is spent.
    prevout.cache = domain::chain::output{};
    prevout.coinbase = false;
    prevout.height = 0;
    prevout.median_time_past = 0;

    // If the input is a coinbase there is no prevout to populate.
    if (outpoint.is_null()) {
        return;
    }

    // Get the input's previous output and its validation metadata.
    auto const count = size();
    auto const& blocks = *blocks_;

    // Reverse iterate because of BIP30.
    for (size_t forward = 0; forward < count; ++forward) {
        size_t const index = count - forward - 1u;
        auto const& txs = blocks[index]->transactions();
        prevout.coinbase = true;

        for (auto const& tx: txs) {
            // Found the prevout at or below the indexed block.
            if (outpoint.hash() == tx.hash() && outpoint.index() < tx.outputs().size()) {
                prevout.height = height_at(index);
                prevout.median_time_past = median_time_past_at(index);
                prevout.cache = tx.outputs()[outpoint.index()];
                return;
            }
            prevout.coinbase = false;
        }
    }
}

//TODO(legacy): use the type alias instead of the std::unord...

// TODO(legacy): absorb into the main chain for speed and code consolidation.
void branch::populate_prevout(output_point const& outpoint, std::vector<std::unordered_map<point, output const*>> const& branch_utxo) const {
    auto& prevout = outpoint.validation;

    // In case this input is a coinbase or the prevout is spent.
    prevout.cache = domain::chain::output{};
    prevout.coinbase = false;
    prevout.height = 0;
    prevout.median_time_past = 0;

    // If the input is a coinbase there is no prevout to populate.
    if (outpoint.is_null()) {
        return;
    }

    // // Get the input's previous output and its validation metadata.
    // auto const count = size();
    // auto const& blocks = *blocks_;

    // // Reverse iterate because of BIP30.
    // for (size_t forward = 0; forward < count; ++forward) {
    //     size_t const index = count - forward - 1u;
    //     auto const& txs = blocks[index]->transactions();

    //     prevout.coinbase = true;
    //     for (auto const& tx: txs) {
    //         // Found the prevout at or below the indexed block.
    //         if (outpoint.hash() == tx.hash() && outpoint.index() < tx.outputs().size()) {
    //             prevout.height = height_at(index);
    //             prevout.median_time_past = median_time_past_at(index);
    //             prevout.cache = tx.outputs()[outpoint.index()];
    //             return;
    //         }
    //         prevout.coinbase = false;
    //     }
    // }


    // Get the input's previous output and its validation metadata.
    auto const count = size();
    auto const& blocks = *blocks_;

    // Reverse iterate because of BIP30.
    for (size_t forward = 0; forward < count; ++forward) {
        size_t const index = count - forward - 1u;
        auto const& txs = blocks[index]->transactions();
        auto const& local_utxo = branch_utxo[index];

        prevout.coinbase = false;
        auto it = local_utxo.find(outpoint);
        if (it != local_utxo.end()) {
            prevout.height = height_at(index);
            prevout.median_time_past = median_time_past_at(index);
            prevout.cache = *it->second;
            prevout.coinbase = it->first.hash() == txs[0].hash();
            return;
        }
    }
}


// TODO(legacy): absorb into the main chain for speed and code consolidation.
// The bits of the block at the given height in the branch.
bool branch::get_bits(uint32_t& out_bits, size_t height) const {
    if (height <= height_) {
        return false;
    }

    auto const block = (*blocks_)[index_of(height)];
    if ( ! block) {
        return false;
    }

    out_bits = block->header().bits();
    return true;
}

// TODO(legacy): absorb into the main chain for speed and code consolidation.
// The version of the block at the given height in the branch.
bool branch::get_version(uint32_t& out_version, size_t height) const {
    if (height <= height_) {
        return false;
    }

    auto const block = (*blocks_)[index_of(height)];

    if ( ! block) {
        return false;
    }

    out_version = block->header().version();
    return true;
}

// TODO(legacy): absorb into the main chain for speed and code consolidation.
// The timestamp of the block at the given height in the branch.
bool branch::get_timestamp(uint32_t& out_timestamp, size_t height) const {
    if (height <= height_) {
        return false;
    }

    auto const block = (*blocks_)[index_of(height)];

    if ( ! block) {
        return false;
    }

    out_timestamp = block->header().timestamp();
    return true;
}

// TODO(legacy): convert to a direct block pool query when the branch goes away.
// The hash of the block at the given height if it exists in the branch.
bool branch::get_block_hash(hash_digest& out_hash, size_t height) const {
    if (height <= height_) {
        return false;
    }

    auto const block = (*blocks_)[index_of(height)];

    if ( ! block) {
        return false;
    }

    out_hash = block->hash();
    return true;
}

} // namespace kth::blockchain
