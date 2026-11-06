// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_QUERY_HPP
#define KTH_NODE_RPC_QUERY_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <kth/node/rpc/chain_progress.hpp>
#include <string>
#include <string_view>
#include <vector>

#include <kth/blockchain/header_index.hpp>
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

// The connected tip fields use one published chain view. The header height
// is captured separately from the active header chain.
struct blockchain_info {
    std::string_view chain;
    size_t blocks;                  // connected tip height
    size_t headers;
    hash_digest best_block_hash;
    double difficulty;                   // of the next required work
    uint32_t median_time;           // median time past of the connected tip
    std::optional<uint256_t> chain_work; // absent while the header index cannot name the tip
    uint64_t size_on_disk;          // block and undo files
    bool initial_block_download;         // the node is not yet synchronized
    std::optional<chain_progress> progress = std::nullopt;
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

// A transaction as the decoderawtransaction result object: txid, hash,
// version, size, locktime, vin and vout, with each output's value in BCH, its
// script, its kind and addresses, and its token data. Addresses are cashaddr
// under `net`'s prefix. Script text is kth's own notation (script::to_string
// under `flags`); the hex is there for anything that reads the bytes.
std::string render_transaction(domain::chain::transaction const& tx,
                               domain::config::network net, domain::script_flags_t flags);

// The getnetworkhashps result: the work the blocks of a window add, from the
// chain work before them to the chain work at its last block, over the time
// the window's timestamps span. 0 when they span no time.
std::string render_network_hash_ps(uint256_t const& work_before, uint256_t const& work_at_end,
                                   uint32_t min_time, uint32_t max_time);

// The estimatefee result: `byte_fee_satoshis` per byte as BCH per kB, rounded
// to whole satoshis per kB.
std::string render_estimate_fee(float byte_fee_satoshis);

// Owned records from the header index; RPC only renders their wire format.
using chain_tip = blockchain::header_index::chain_tip;

// Serialize the getchaintips result array, in the order given.
std::string render_chain_tips(std::vector<chain_tip> const& tips);

// The gettxout result object for an unspent output: the tip it was read
// against, its confirmations (0 for an output of a mempool transaction), its
// value in BCH, its scriptPubKey and token data as decoderawtransaction shows
// them, and whether a coinbase created it.
std::string render_tx_out(hash_digest const& best_block, int64_t confirmations,
                          domain::chain::output const& output, bool coinbase,
                          domain::config::network net, domain::script_flags_t flags,
                          std::optional<int64_t> confirmations_ticks = std::nullopt);

// The getnetworkinfo fields.
struct network_info {
    std::string_view version;        // kth::version, e.g. "1.5.2"
    std::string_view subversion;     // the user agent this node sends its peers
    uint32_t protocol_version;
    uint64_t services;
    bool relay;                      // relays transactions to its peers
    size_t connections;
    bool active = false;
    float byte_fee_satoshis;         // the size part of the transaction price
};

// Serialize the getnetworkinfo result object: `version` as one number,
// major * 1000000 + minor * 10000 + patch * 100, the services as 16 hex digits
// with their names, and `relayfee` in BCH per kB.
std::string render_network_info(network_info const& info);

// One transaction of verbose getrawmempool, with what the mempool knows about it.
struct mempool_listing {
    hash_digest txid;
    uint64_t fee;      // satoshis
    uint32_t size;     // serialized bytes
    uint64_t time;     // first seen (unix seconds)
    hash_list depends;
    hash_list spentby;
};

// The verbose getrawmempool object: each transaction keyed by its txid, with
// `fees` (`base` and `modified`, in BCH; the mempool keeps no fee deltas, so
// they are equal), `size`, `time`, `depends` and `spentby`.
std::string render_raw_mempool_verbose(std::vector<mempool_listing> const& listings);
// Where a confirmed transaction sits, for verbose getrawtransaction.
struct transaction_location {
    hash_digest block_hash;
    int64_t confirmations;     // -1 when its block is off the connected chain
    uint32_t block_time;
    std::optional<int64_t> confirmations_ticks = std::nullopt;
};

// The verbose getrawtransaction object: the decoderawtransaction fields, `hex`,
// and for a transaction read from a block its `blockhash`, `confirmations`,
// `time`, `blocktime` and `in_active_chain`. A block off the connected chain
// gives `confirmations` 0, no times and `in_active_chain` false.
std::string render_transaction_verbose(domain::chain::transaction const& tx,
                                       domain::config::network net, domain::script_flags_t flags,
                                       std::optional<transaction_location> const& location);

// What getblock reports about a block beyond the block itself.
struct block_location {
    size_t height;
    int64_t confirmations;            // -1 when off the connected chain
    uint32_t median_time;             // of the 11 blocks ending at this one
    std::optional<uint256_t> chain_work;   // the chain's work up to this block
    std::optional<hash_digest> next_block_hash;
    std::optional<chain_progress> progress = std::nullopt;
    std::optional<int64_t> confirmations_ticks = std::nullopt;
};

// The verbose getblock object. `with_transactions` (verbosity 2) lists each
// transaction as verbose getrawtransaction's fields without the location;
// otherwise (verbosity 1) by txid.
std::string render_block_verbose(domain::chain::block const& block, block_location const& location,
                                 bool with_transactions,
                                 domain::config::network net, domain::script_flags_t flags);

// getblockheader (verbose) result object. `confirmations` is -1 for a block
// that is not on the connected chain.
std::string render_block_header(domain::chain::header const& header,
                                size_t height, hash_digest const& hash,
                                int64_t confirmations,
                                std::optional<chain_progress> progress = std::nullopt,
                                std::optional<int64_t> confirmations_ticks = std::nullopt);

// validateaddress result object for `address` on network `net`: only
// {"isvalid":false} unless it is a P2PKH or P2SH address of that network,
// otherwise also the address as cashaddr under the network's prefix, the
// scriptPubKey it pays to, whether that is a script hash, and whether the
// address signals CashTokens support.
std::string render_validate_address(std::string_view address, domain::config::network net);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_QUERY_HPP
