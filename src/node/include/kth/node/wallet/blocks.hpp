// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_BLOCKS_HPP
#define KTH_NODE_WALLET_BLOCKS_HPP

#include <cstdint>
#include <expected>
#include <system_error>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/define.hpp>

namespace kth::node::wallet {

// What a wallet looks for in a block.

/// The positions of the inputs of `tx` a sender may grind for a reusable
/// payment address (the first 30, none of a coinbase) whose prefix starts
/// with the `bits` bits of `prefix`.
[[nodiscard]]
KND_API
std::vector<uint32_t> prefix_inputs(domain::chain::transaction const& tx, uint16_t prefix, uint8_t bits);

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_BLOCKS_HPP
