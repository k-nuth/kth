// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TICK_SCHEDULE_HPP
#define KTH_BLOCKCHAIN_TICK_SCHEDULE_HPP

#if defined(KTH_CURRENCY_BCH)
#include <expected>
#include <limits>

#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain/chain/block_ticks.hpp>

namespace kth::blockchain {

// Obtain the complete interval schedule of a branch, including a change
// triggered by its tip's own MTP. Building the tip's state would miss that
// change because it describes the interval that preceded the tip; building
// the next candidate supplies the schedule both policy and RPC consumers need.
// The synthetic header is never validated, inserted, or published.
[[nodiscard]] inline
std::expected<domain::chain::block_ticks, code> tick_schedule_for_tip(
    validate_header const& validator, header_index const& index,
    header_index::index_t tip) {
    if (tip == header_index::null_index || tip >= index.size()) {
        return std::unexpected(error::operation_failed);
    }
    auto const height = index.get_height(tip);
    if (height < 0 || height == std::numeric_limits<int32_t>::max()) {
        return std::unexpected(error::operation_failed);
    }
    domain::chain::header const next{
        index.get_version(tip), index.get_hash(tip), null_hash,
        index.get_timestamp(tip), index.get_bits(tip), 0};
    auto const state = validator.chain_state_at(next, null_hash, size_t(height) + 1, tip, index);
    if (!state) {
        return std::unexpected(state.error());
    }
    return (*state)->ticks();
}

} // namespace kth::blockchain
#endif // KTH_CURRENCY_BCH
#endif // KTH_BLOCKCHAIN_TICK_SCHEDULE_HPP
