// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_SERVICE_HPP
#define KTH_NODE_WALLET_SERVICE_HPP

#if ! defined(KTH_WITH_WALLET)
#error "Wallet support is an optional feature: build with the Conan options neutrino=True and wallet=True."
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/define.hpp>
#include <kth/node/wallet/history.hpp>

namespace kth::blockchain {
struct block_chain;
} // namespace kth::blockchain

namespace kth::node::wallet {

// Wallet support in the node: one set of calls for the wallet of the node's
// own user, answered from the node's own chain (its stored blocks, its chain
// state and its mempool). Nothing is asked of any peer, no index is built,
// and nothing kept grows with the chain: the only memory is what the watched
// targets own and a bounded cache of blocks. Other nodes' wallets are never
// answered.

/// A transaction of a target's history in the mempool.
struct unconfirmed_tx {
    hash_digest txid;
    /// It spends an output of another mempool transaction.
    bool unconfirmed_parent;
    std::optional<uint64_t> fee;
    std::vector<received> outputs;
    std::vector<spent> inputs;
};

struct target_history {
    /// The blocks up to this height are in `confirmed`.
    std::optional<uint32_t> synced;
    /// In chain order.
    std::vector<confirmed_tx> confirmed;
    /// Those without unconfirmed parents first, then by transaction hash.
    std::vector<unconfirmed_tx> unconfirmed;
};

/// Satoshis.
struct balance {
    /// Unspent confirmed outputs; mempool spends not deducted.
    uint64_t confirmed;
    /// What the mempool adds minus what it spends.
    int64_t unconfirmed;
};

struct confirmation {
    uint32_t height;
    hash_digest block;
    uint32_t position;
};

struct transaction_info {
    domain::chain::transaction tx;
    /// None while in the mempool.
    std::optional<confirmation> confirmed;
};

/// A confirmed transaction's Merkle branch to its block header.
struct merkle_proof {
    hash_digest txid;
    confirmation confirmed;
    /// Sibling hashes from the leaves up.
    hash_list branch;
};

/// A transaction one of whose first 30 inputs starts with an RPA prefix.
struct rpa_match {
    hash_digest txid;
    /// None while in the mempool.
    std::optional<uint32_t> height;
    std::vector<uint32_t> inputs;
};

/// Fee rates in satoshis per 1000 bytes.
struct fee_estimate {
    /// The lowest rate relayed: the node's own relay floor, or the highest
    /// fee filter peers sent (1000 before any).
    uint64_t relay;
    /// What BCHN's estimatefee answers: the mempool's minimum, which is the
    /// relay floor while blocks have room (BCH has no fee market to guess).
    uint64_t estimate;
};

/// A double-spend proof relayed by peers (or held by the node's mempool).
struct dsproof_info {
    hash_digest dspid;
    /// The mempool transaction that spends the outpoint the proof is about.
    hash_digest txid;
    data_chunk raw;
    domain::chain::point outpoint;
    /// That transaction and its mempool descendants.
    std::vector<hash_digest> descendants;
};

/// The count of changes, and the keys changed since the count asked about
/// (watched targets' keys and transaction hashes), sorted; none when anything
/// may have changed (a new tip, a reorganization).
struct touched_keys {
    uint64_t count = 0;
    std::optional<std::vector<hash_digest>> keys;
};

struct mempool_summary {
    size_t count;
    size_t bytes;
    /// Satoshis per byte and size, of each transaction whose fee is known.
    std::vector<std::pair<double, size_t>> fee_rates;
};

struct status {
    /// "chain" on a full node, "peers" otherwise.
    std::string source;
    uint32_t header_height = 0;
    /// The height the answers reach: the filter tip (peers) or the connected tip (chain).
    std::optional<uint32_t> tip;
    std::optional<uint32_t> input_prefix_tip;
    size_t peers = 0;
    bool synced = false;
    /// Whether filters carry transaction keys (kind 4), once a block has shown it.
    std::optional<bool> transaction_keys;
    uint64_t bytes_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t blocks_downloaded = 0;
    uint64_t filter_bytes = 0;
    std::string error;
    /// Peers found serving what does not fit the chain, each as
    /// "<endpoint>: <why>"; none of them serves this service again.
    std::vector<std::string> refused;
};

/// How a full node answers its wallet from its own chain. A target is read
/// by parsing the stored blocks from its birthday on.
struct chain_settings {
    /// The first height a target is read from when it is watched without one.
    uint32_t birthday = 0;
    /// Blocks read at once during a scan.
    size_t parallel_reads = 8;
    size_t mempool_bytes = size_t(256) << 20;
};

struct KND_API service {
    /// Answers from the node's own chain: its stored blocks, its chain state
    /// and its mempool. Nothing is sent to any peer and nothing is stored.
    [[nodiscard]]
    static std::unique_ptr<service> from_chain(blockchain::block_chain& chain, domain::config::network network,
        chain_settings settings = {});

    ~service();
    service(service const&) = delete;
    service& operator=(service const&) = delete;

    /// The executor every operation runs on.
    [[nodiscard]] ::asio::any_io_executor executor() const;

    /// Keeps the answers current until stop(): headers, filters, the tip, relay.
    ::asio::awaitable<std::error_code> run();

    /// Ends run() and every operation waiting on peers. Thread safe.
    void stop();

    /// Where the service stands. Thread safe.
    [[nodiscard]] status current() const;

    /// Waits until the answers reach the peers' tip. channel_timeout after `timeout`.
    ::asio::awaitable<std::expected<status, std::error_code>> wait_synced(std::chrono::seconds timeout);

    /// Watches targets from `birthday` on (at least the service's own birthday).
    ::asio::awaitable<std::error_code> watch(std::vector<target> targets, uint32_t birthday);
    ::asio::awaitable<bool> unwatch(target watched);

    /// A target's history; a target not watched yet is watched from the service's birthday.
    ::asio::awaitable<std::expected<target_history, std::error_code>> history(target watched);
    ::asio::awaitable<std::expected<balance, std::error_code>> get_balance(target watched, token_filter tokens);

    /// Confirmed (by height) first, then unconfirmed; outputs a mempool
    /// transaction spends are left out.
    ::asio::awaitable<std::expected<std::vector<utxo>, std::error_code>> unspent(target watched, token_filter tokens);

    /// A transaction by hash: from the mempool, a watched history, the block
    /// at `height_hint`, or the stored blocks read from the tip down (a fully
    /// spent transaction included). No index.
    ::asio::awaitable<std::expected<transaction_info, std::error_code>> transaction(hash_digest txid, std::optional<uint32_t> height_hint);
    ::asio::awaitable<std::expected<merkle_proof, std::error_code>> merkle(hash_digest txid, std::optional<uint32_t> height_hint);
    ::asio::awaitable<std::expected<std::pair<domain::chain::transaction, merkle_proof>, std::error_code>> transaction_at(uint32_t height, uint32_t position);

    /// Transactions with one of their first 30 inputs in the prefix, confirmed in [from, to] (to: the tip when absent).
    ::asio::awaitable<std::expected<std::vector<rpa_match>, std::error_code>> rpa_history(uint16_t prefix, uint8_t bits, uint32_t from, std::optional<uint32_t> to);
    ::asio::awaitable<std::vector<rpa_match>> rpa_mempool(uint16_t prefix, uint8_t bits);

    /// Sends a transaction and waits for it to be accepted: on a full node, by
    /// the node's own mempool, which relays it.
    ::asio::awaitable<std::expected<hash_digest, std::error_code>> broadcast(domain::chain::transaction tx);

    ::asio::awaitable<fee_estimate> fees();
    ::asio::awaitable<mempool_summary> mempool();
    ::asio::awaitable<std::optional<domain::chain::header>> header(uint32_t height);

    /// The Merkle branch of the hash of block `height` in the tree of the block
    /// hashes 0 to `cp_height`, and that tree's root (Electrum's header proofs).
    ::asio::awaitable<std::expected<std::pair<hash_list, hash_digest>, std::error_code>> header_branch(uint32_t height, uint32_t cp_height);

    /// An output that exists and nothing spends, confirmed or in the mempool;
    /// none when it is spent or does not exist.
    ::asio::awaitable<std::expected<std::optional<utxo>, std::error_code>> output(domain::chain::point outpoint);

    /// The double-spend proof with this hash, or the one about this
    /// transaction or one of its ancestors in the mempool.
    ::asio::awaitable<std::optional<dsproof_info>> dsproof(hash_digest dspid_or_txid);

    /// Every mempool transaction a double-spend proof is about, with its descendants.
    ::asio::awaitable<std::vector<hash_digest>> dsproof_transactions();

    /// The number of changes so far (a new tip, a reorganization, a mempool
    /// transaction in or out, a double-spend proof), as soon as it is above
    /// `seen`, or after `timeout`. What follows the wallet waits on this
    /// instead of asking again and again.
    ::asio::awaitable<uint64_t> changed(uint64_t seen, std::chrono::seconds timeout);
    /// What changed after the count `seen`: lets a follower skip the targets
    /// and transactions a change did not touch.
    ::asio::awaitable<touched_keys> touched(uint64_t seen);

    struct impl;

private:
    explicit service(std::unique_ptr<impl> state);
    std::unique_ptr<impl> impl_;
};

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_SERVICE_HPP
