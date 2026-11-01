// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_QUERY_HPP
#define KTH_NODE_RPC_QUERY_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/math/hash.hpp>

// The pure (chain-free) serialization behind the blockchain query RPCs, split
// out so it can be unit-tested without a live block_chain.

namespace kth::blockchain {
struct mempool_totals;
struct mempool_entry_info;
} // namespace kth::blockchain

namespace kth::node::rpc {

// Wire-serialize a transaction / block to a lowercase hex string
// (getrawtransaction / getblock).
std::string transaction_to_hex(domain::chain::transaction const& tx);
std::string block_to_hex(domain::chain::block const& block);

// The 80-byte wire-serialized block header as hex (getblockheader with
// verbose=false).
std::string header_to_hex(domain::chain::header const& header);

// The getblockchaininfo fields. The tip fields (blocks through chain_work) are
// read from one published chain view, so that they describe one chain.
struct blockchain_info {
    std::string_view chain;
    std::size_t blocks;                  // connected tip height
    std::size_t headers;
    hash_digest best_block_hash;
    double difficulty;                   // of the next required work
    std::uint32_t median_time;           // median time past of the connected tip
    std::optional<uint256_t> chain_work; // absent while the header index cannot name the tip
    std::uint64_t size_on_disk;          // block and undo files
    bool initial_block_download;         // the node is not yet synchronized
};

// Serialize the getblockchaininfo result object. verificationprogress is the
// connected tip's share of the header chain, by height.
std::string render_blockchain_info(blockchain_info const& info);

// A JSON array of display-order hashes (getrawmempool, getmempoolancestors,
// getmempooldescendants).
std::string render_hash_list(hash_list const& hashes);

// getmempoolinfo / getmempoolentry result objects.
std::string render_mempool_info(blockchain::mempool_totals const& totals);
std::string render_mempool_entry(blockchain::mempool_entry_info const& entry,
                                 hash_list const& depends, hash_list const& spentby);

// getblockheader (verbose) result object.
std::string render_block_header(domain::chain::header const& header,
                                std::size_t height, hash_digest const& hash);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_QUERY_HPP
