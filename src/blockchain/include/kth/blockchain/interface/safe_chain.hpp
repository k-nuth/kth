// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_SAFE_CHAIN_HPP
#define KTH_BLOCKCHAIN_SAFE_CHAIN_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <kth/domain.hpp>
// #include <kth/infrastructure.hpp>
#include <kth/infrastructure/handlers.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool_transaction_summary.hpp>

namespace kth::blockchain {

/// This interface is thread safe.
/// A high level interface for encapsulation of the blockchain database.
/// Implementations are expected to be thread safe.
struct KB_API safe_chain {
    using result_handler = handle0;

    /// Object fetch handlers.
    using last_height_fetch_handler = handle1<size_t>;
    using block_height_fetch_handler = handle1<size_t>;
    using output_fetch_handler = handle1<domain::chain::output>;
    using spend_fetch_handler = handle1<domain::chain::input_point>;
    using history_fetch_handler = handle1<domain::chain::history_compact::list>;
    using stealth_fetch_handler = handle1<domain::chain::stealth_compact::list>;
    using transaction_index_fetch_handler = handle2<size_t, size_t>;

    using confirmed_transactions_fetch_handler = handle1<std::vector<hash_digest>>;
    // Smart pointer parameters must not be passed by reference.
    using block_fetch_handler = std::function<void(code const&, block_const_ptr, size_t)>;
    using block_header_txs_size_fetch_handler = std::function<void(code const&, header_const_ptr, size_t, std::shared_ptr<hash_list>, uint64_t)>;
    using block_hash_time_fetch_handler = std::function<void(code const&, hash_digest const&, uint32_t, size_t)>;
    using merkle_block_fetch_handler =  std::function<void(code const&, merkle_block_ptr, size_t)>;
    using compact_block_fetch_handler = std::function<void(code const&, compact_block_ptr, size_t)>;
    using block_header_fetch_handler = std::function<void(code const&, header_ptr, size_t)>;
    using transaction_fetch_handler = std::function<void(code const&, transaction_const_ptr, size_t, size_t)>;
    using ds_proof_fetch_handler = std::function<void(code const&, double_spend_proof_const_ptr)>;
    using transaction_unconfirmed_fetch_handler = std::function<void(code const&, transaction_const_ptr)>;

    using locator_block_headers_fetch_handler = std::function<void(code const&, headers_ptr)>;
    using block_locator_fetch_handler = std::function<void(code const&, get_headers_ptr)>;
    using inventory_fetch_handler = std::function<void(code const&, inventory_ptr)>;

    /// Subscription handlers.
    using reorganize_handler = std::function<bool(code, size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr)>;
    using transaction_handler = std::function<bool(code, transaction_const_ptr)>;
    using ds_proof_handler = std::function<bool(code, double_spend_proof_const_ptr)>;

    using for_each_tx_handler = std::function<void(code const&, size_t, domain::chain::transaction const&)>;
    using mempool_mini_hash_map = std::unordered_map<mini_hash, domain::chain::transaction>;

    // Startup and shutdown.
    // ------------------------------------------------------------------------

    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool close() = 0;

    // Node Queries.
    // ------------------------------------------------------------------------


    virtual void fetch_block(size_t height, block_fetch_handler handler) const = 0;

    virtual void fetch_block(hash_digest const& hash, block_fetch_handler handler) const = 0;

    virtual void fetch_locator_block_hashes(get_blocks_const_ptr locator, hash_digest const& threshold, size_t limit, inventory_fetch_handler handler) const = 0;

    virtual void fetch_merkle_block(size_t height, merkle_block_fetch_handler handler) const = 0;

    virtual void fetch_merkle_block(hash_digest const& hash, merkle_block_fetch_handler handler) const = 0;

    virtual void fetch_compact_block(size_t height, compact_block_fetch_handler handler) const = 0;

    virtual void fetch_compact_block(hash_digest const& hash, compact_block_fetch_handler handler) const = 0;

    virtual void fetch_block_header_txs_size(hash_digest const& hash, block_header_txs_size_fetch_handler handler) const = 0;

    virtual void fetch_ds_proof(hash_digest const& hash, ds_proof_fetch_handler handler) const = 0;

    virtual void fetch_transaction(hash_digest const& hash, bool require_confirmed, transaction_fetch_handler handler) const = 0;

    virtual void fetch_transaction_position(hash_digest const& hash, bool require_confirmed, transaction_index_fetch_handler handler) const = 0;

    // virtual void for_each_transaction(size_t from, size_t to, for_each_tx_handler const& handler) const = 0;

    // virtual void for_each_transaction_non_coinbase(size_t from, size_t to, for_each_tx_handler const& handler) const = 0;

    virtual void fetch_locator_block_headers(get_headers_const_ptr locator, hash_digest const& threshold, size_t limit, locator_block_headers_fetch_handler handler) const = 0;

    virtual void fetch_block_locator(domain::chain::block::indexes const& heights, block_locator_fetch_handler handler) const = 0;

    virtual void fetch_last_height(last_height_fetch_handler handler) const = 0;

    virtual void fetch_block_header(size_t height, block_header_fetch_handler handler) const = 0;

    virtual void fetch_block_header(hash_digest const& hash, block_header_fetch_handler handler) const = 0;

    virtual bool get_block_hash(hash_digest& out_hash, size_t height) const = 0;

    virtual void fetch_block_height(hash_digest const& hash, block_height_fetch_handler handler) const = 0;

    virtual void fetch_block_hash_timestamp(size_t height, block_hash_time_fetch_handler handler) const = 0;

    // Server Queries.
    //-------------------------------------------------------------------------

    virtual void fetch_spend(const domain::chain::output_point& outpoint, spend_fetch_handler handler) const = 0;

    virtual void fetch_history(short_hash const& address_hash, size_t limit, size_t from_height, history_fetch_handler handler) const = 0;
    virtual void fetch_confirmed_transactions(short_hash const& address_hash, size_t limit, size_t from_height, confirmed_transactions_fetch_handler handler) const = 0;

    // virtual void fetch_stealth(binary const& filter, size_t from_height, stealth_fetch_handler handler) const = 0;

    // Transaction Pool.
    //-------------------------------------------------------------------------

    virtual void fetch_template(merkle_block_fetch_handler handler) const = 0;
    virtual void fetch_mempool(size_t count_limit, uint64_t minimum_fee, inventory_fetch_handler handler) const = 0;

    virtual std::vector<mempool_transaction_summary> get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const = 0;

    virtual std::vector<mempool_transaction_summary> get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const = 0;

    virtual std::vector<domain::chain::transaction> get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const = 0;

    virtual void fetch_unconfirmed_transaction(hash_digest const& hash, transaction_unconfirmed_fetch_handler handler) const = 0;

    virtual mempool_mini_hash_map get_mempool_mini_hash_map(domain::message::compact_block const& block) const = 0;

    virtual void fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const = 0;


    // Filters.
    //-------------------------------------------------------------------------

    virtual void filter_blocks(get_data_ptr message, result_handler handler) const = 0;

    virtual void filter_transactions(get_data_ptr message, result_handler handler) const = 0;

    // Subscribers.
    //-------------------------------------------------------------------------

    virtual void subscribe_blockchain(reorganize_handler&& handler) = 0;
    virtual void subscribe_transaction(transaction_handler&& handler) = 0;
    virtual void subscribe_ds_proof(ds_proof_handler&& handler) = 0;
    virtual void unsubscribe() = 0;


    // Transaction Validation.
    //-----------------------------------------------------------------------------

    virtual void transaction_validate(transaction_const_ptr tx, result_handler handler) const = 0;

    // Organizers.
    //-------------------------------------------------------------------------

    virtual void organize(block_const_ptr block, result_handler handler) = 0;
    virtual void organize(transaction_const_ptr tx, result_handler handler) = 0;
    virtual void organize(double_spend_proof_const_ptr ds_proof, result_handler handler) = 0;

    // Properties
    // ------------------------------------------------------------------------

    virtual bool is_stale() const = 0;

    //TODO(Mario) temporary duplication
    /// Get a determination of whether the block hash exists in the store.
    virtual bool get_block_exists_safe(hash_digest const& block_hash) const = 0;

};

} // namespace kth::blockchain

#endif
