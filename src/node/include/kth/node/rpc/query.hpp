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
#include <kth/domain/config/network.hpp>
#include <kth/domain/machine/script_flags.hpp>
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

// Serialize the getblockchaininfo result object.
std::string render_blockchain_info(
    std::string_view chain, std::size_t blocks, std::size_t headers,
    std::string_view best_block_hash, double difficulty);

// A JSON array of display-order hashes (getrawmempool, getmempoolancestors,
// getmempooldescendants).
std::string render_hash_list(hash_list const& hashes);

// getmempoolinfo / getmempoolentry result objects.
std::string render_mempool_info(blockchain::mempool_totals const& totals);
std::string render_mempool_entry(blockchain::mempool_entry_info const& entry,
                                 hash_list const& depends, hash_list const& spentby);

// A transaction as the decoderawtransaction result object: txid, hash,
// version, size, locktime, vin and vout, with each output's value in BCH, its
// script, its kind and addresses, and its token data. Addresses are cashaddr
// under `net`'s prefix. Script text is kth's own notation (script::to_string
// under `flags`); the hex is there for anything that reads the bytes.
std::string render_transaction(domain::chain::transaction const& tx,
                               domain::config::network net, domain::script_flags_t flags);

// Where a confirmed transaction sits, for verbose getrawtransaction.
struct transaction_location {
    hash_digest block_hash;
    std::int64_t confirmations;     // -1 when its block is off the connected chain
    std::uint32_t block_time;
};

// The verbose getrawtransaction object: the decoderawtransaction fields, `hex`,
// and for a transaction read from a block its `blockhash`, `confirmations`,
// `time` and `blocktime`.
std::string render_transaction_verbose(domain::chain::transaction const& tx,
                                       domain::config::network net, domain::script_flags_t flags,
                                       std::optional<transaction_location> const& location);

// What getblock reports about a block beyond the block itself.
struct block_location {
    std::size_t height;
    std::int64_t confirmations;            // -1 when off the connected chain
    std::uint32_t median_time;             // of the 11 blocks ending at this one
    std::optional<uint256_t> chain_work;   // the chain's work up to this block
    std::optional<hash_digest> next_block_hash;
};

// The verbose getblock object. `with_transactions` (verbosity 2) lists each
// transaction as verbose getrawtransaction's fields without the location;
// otherwise (verbosity 1) by txid.
std::string render_block_verbose(domain::chain::block const& block, block_location const& location,
                                 bool with_transactions,
                                 domain::config::network net, domain::script_flags_t flags);

// getblockheader (verbose) result object.
std::string render_block_header(domain::chain::header const& header,
                                std::size_t height, hash_digest const& hash);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_QUERY_HPP
