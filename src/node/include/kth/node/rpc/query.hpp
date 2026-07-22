// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_QUERY_HPP
#define KTH_NODE_RPC_QUERY_HPP

#include <cstddef>
#include <string>
#include <string_view>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/transaction.hpp>

// The pure (chain-free) serialization behind the blockchain query RPCs, split
// out so it can be unit-tested without a live block_chain.

namespace kth::node::rpc {

// Wire-serialize a transaction / block to a lowercase hex string
// (getrawtransaction / getblock).
std::string transaction_to_hex(domain::chain::transaction const& tx);
std::string block_to_hex(domain::chain::block const& block);

// Serialize the getblockchaininfo result object.
std::string render_blockchain_info(
    std::string_view chain, std::size_t blocks, std::size_t headers,
    std::string_view best_block_hash, double difficulty);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_QUERY_HPP
