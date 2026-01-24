// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP
#define KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <expected>
#include <vector>

#include <kth/infrastructure/utility/atomic.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/database.hpp>
#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/pools/block_organizer.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/pools/mempool_transaction_summary.hpp>
#include <kth/blockchain/pools/transaction_organizer.hpp>
#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

using kth::awaitable_expected;
using database::heights_t;

/// Unified blockchain interface.
/// Thread safety: get_* methods are NOT thread safe, fetch_* methods are thread safe.
struct KB_API block_chain {
    using executor_type = ::asio::any_io_executor;

    //TODO: unordered_flat_map or concurrent_flat_map  (do we need concurrency here?)
    using mempool_mini_hash_map = std::unordered_map<mini_hash, domain::chain::transaction>;

    // =========================================================================
    // CONSTRUCTION
    // =========================================================================

    block_chain(threadpool& pool,
                blockchain::settings const& chain_settings,
                database::settings const& database_settings,
                domain::config::network network,
                bool relay_transactions = true);

    ~block_chain();

    // Non-copyable, non-movable
    block_chain(block_chain const&) = delete;
    block_chain& operator=(block_chain const&) = delete;
    block_chain(block_chain&&) = delete;
    block_chain& operator=(block_chain&&) = delete;

    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    [[nodiscard]] bool start();
    [[nodiscard]] bool stop();
    [[nodiscard]] bool close();
    [[nodiscard]] bool stopped() const;

    // =========================================================================
    // ORGANIZERS (Core blockchain operations)
    // =========================================================================

    /// @param headers_pre_validated If true, skip header validation (for headers-first sync)
    [[nodiscard]]
    ::asio::awaitable<code> organize(block_const_ptr block, bool headers_pre_validated = false);

    /// Fast IBD: store only block data without validation or UTXO updates.
    /// For use under checkpoint where we trust the blocks.
    /// @param block The block to store
    /// @param height The height at which to store the block
    [[nodiscard]]
    ::asio::awaitable<code> organize_fast(block_const_ptr block, size_t height);

    [[nodiscard]]
    ::asio::awaitable<code> organize(transaction_const_ptr tx);

    [[nodiscard]]
    ::asio::awaitable<code> organize(double_spend_proof_const_ptr ds_proof);

    // Headers-first sync: organize a header without full block data
    [[nodiscard]]
    ::asio::awaitable<code> organize_header(header_const_ptr header);

    // Headers-first sync: organize multiple headers in a single batch
    [[nodiscard]]
    code organize_headers_batch(domain::chain::header::list const& headers, size_t start_height);

#if ! defined(KTH_DB_READONLY)
    [[nodiscard]]
    awaitable_expected<block_const_ptr_list_ptr> reorganize(
        infrastructure::config::checkpoint const& fork_point,
        block_const_ptr_list_const_ptr incoming_blocks);

    [[nodiscard]]
    ::asio::awaitable<code> push(transaction_const_ptr tx);

    [[nodiscard]] code push_sync(transaction_const_ptr tx);
    [[nodiscard]] bool insert(block_const_ptr block, size_t height);
    void prune_reorg_async();

    // Apply a batch of UTXO changes (for UTXO set building after fast IBD)
    [[nodiscard]]
    database::result_code apply_utxo_delta(
        boost::unordered_flat_map<domain::chain::point, database::utxo_entry> const& inserts,
        boost::unordered_flat_set<domain::chain::point> const& deletes
    );

    // Get/set the last block height for which UTXO set was built
    [[nodiscard]]
    std::expected<uint32_t, database::result_code> get_utxo_built_height() const;

    [[nodiscard]]
    database::result_code set_utxo_built_height(uint32_t height);

    // TODO(fernando): TEMPORARY - REMOVE THIS METHOD AFTER TESTING UTXO BUILD
    [[nodiscard]]
    database::result_code clear_utxo_set();
#endif

    // =========================================================================
    // CHAIN STATE
    // =========================================================================

    domain::chain::chain_state::ptr chain_state() const;
    domain::chain::chain_state::ptr chain_state(branch::const_ptr branch) const;

    // =========================================================================
    // SUBSCRIPTIONS
    // =========================================================================

    using block_channel_ptr = block_organizer::block_broadcaster::channel_ptr;
    using transaction_channel_ptr = transaction_organizer::transaction_broadcaster::channel_ptr;
    using ds_proof_channel_ptr = transaction_organizer::ds_proof_broadcaster::channel_ptr;

    [[nodiscard]]
    block_channel_ptr subscribe_blockchain();
    [[nodiscard]]
    transaction_channel_ptr subscribe_transaction();
    [[nodiscard]]
    ds_proof_channel_ptr subscribe_ds_proof();

    void unsubscribe_blockchain(block_channel_ptr const& channel);
    void unsubscribe_transaction(transaction_channel_ptr const& channel);
    void unsubscribe_ds_proof(ds_proof_channel_ptr const& channel);

    // =========================================================================
    // VALIDATION
    // =========================================================================

    [[nodiscard]]
    ::asio::awaitable<code> transaction_validate(transaction_const_ptr tx) const;

    // =========================================================================
    // PROPERTIES
    // =========================================================================

    bool is_stale() const;
    settings const& chain_settings() const;
    executor_type executor() const;

    /// Access the header index (for headers-first sync).
    [[nodiscard]] header_index& headers() { return header_index_; }
    [[nodiscard]] header_index const& headers() const { return header_index_; }

#if defined(KTH_WITH_MEMPOOL)
    std::pair<std::vector<kth::mining::transaction_element>, uint64_t> get_block_template() const;
#endif

    // =========================================================================
    // DATABASE READERS (Low-level, NOT thread safe)
    // =========================================================================

    [[nodiscard]] std::expected<heights_t, database::result_code> get_last_heights() const;
    [[nodiscard]] std::expected<domain::chain::header, database::result_code> get_header(size_t height) const;
    [[nodiscard]] std::expected<database::header_with_abla_state_t, database::result_code> get_header_and_abla_state(size_t height) const;
    [[nodiscard]] std::expected<domain::chain::header::list, database::result_code> get_headers(size_t from, size_t to) const;
    [[nodiscard]] std::expected<size_t, database::result_code> get_height(hash_digest const& block_hash) const;
    [[nodiscard]] std::expected<uint32_t, database::result_code> get_bits(size_t height) const;
    [[nodiscard]] std::expected<uint32_t, database::result_code> get_timestamp(size_t height) const;
    [[nodiscard]] std::expected<uint32_t, database::result_code> get_version(size_t height) const;
    [[nodiscard]] std::expected<hash_digest, database::result_code> get_block_hash(size_t height) const;
    [[nodiscard]] std::expected<uint256_t, database::result_code> get_branch_work(uint256_t const& maximum, size_t height) const;

    struct output_info {
        domain::chain::output output;
        size_t height;
        uint32_t median_time_past;
        bool coinbase;
    };

    [[nodiscard]] std::expected<output_info, database::result_code> get_output(
        domain::chain::output_point const& outpoint,
        size_t branch_height, bool require_confirmed) const;

    [[nodiscard]] std::expected<output_info, database::result_code> get_utxo(
        domain::chain::output_point const& outpoint, size_t branch_height) const;

    [[nodiscard]] std::expected<database::internal_database::utxo_pool_t, database::result_code> get_utxo_pool_from(uint32_t from, uint32_t to) const;

    [[nodiscard]] std::expected<std::pair<size_t, size_t>, database::result_code> get_transaction_position(
        hash_digest const& hash, bool require_confirmed) const;

    [[nodiscard]] bool header_exists(hash_digest const& block_hash) const;
    [[nodiscard]] bool block_exists(hash_digest const& block_hash) const;

    // =========================================================================
    // FETCH OPERATIONS (Thread safe, coroutine-based)
    // =========================================================================

    // Block fetching
    [[nodiscard]] awaitable_expected<std::pair<block_const_ptr, size_t>>
    fetch_block(size_t height) const;

    [[nodiscard]] awaitable_expected<std::pair<block_const_ptr, size_t>>
    fetch_block(hash_digest const& hash) const;

    [[nodiscard]] awaitable_expected<std::pair<header_ptr, size_t>>
    fetch_block_header(size_t height) const;

    [[nodiscard]] awaitable_expected<std::pair<header_ptr, size_t>>
    fetch_block_header(hash_digest const& hash) const;

    [[nodiscard]] awaitable_expected<size_t>
    fetch_block_height(hash_digest const& hash) const;

    [[nodiscard]] awaitable_expected<std::tuple<hash_digest, uint32_t, size_t>>
    fetch_block_hash_timestamp(size_t height) const;

    [[nodiscard]] awaitable_expected<std::tuple<header_const_ptr, size_t, std::shared_ptr<hash_list>, uint64_t>>
    fetch_block_header_txs_size(hash_digest const& hash) const;

    [[nodiscard]] awaitable_expected<heights_t>
    fetch_last_height() const;

    // Merkle/Compact blocks
    [[nodiscard]] awaitable_expected<std::pair<merkle_block_ptr, size_t>>
    fetch_merkle_block(size_t height) const;

    [[nodiscard]] awaitable_expected<std::pair<merkle_block_ptr, size_t>>
    fetch_merkle_block(hash_digest const& hash) const;

    [[nodiscard]] awaitable_expected<std::pair<compact_block_ptr, size_t>>
    fetch_compact_block(size_t height) const;

    [[nodiscard]] awaitable_expected<std::pair<compact_block_ptr, size_t>>
    fetch_compact_block(hash_digest const& hash) const;

    // Transaction fetching
    [[nodiscard]] awaitable_expected<std::tuple<transaction_const_ptr, size_t, size_t>>
    fetch_transaction(hash_digest const& hash, bool require_confirmed) const;

    [[nodiscard]] awaitable_expected<std::pair<size_t, size_t>>
    fetch_transaction_position(hash_digest const& hash, bool require_confirmed) const;

    [[nodiscard]] awaitable_expected<transaction_const_ptr>
    fetch_unconfirmed_transaction(hash_digest const& hash) const;

    // Locator operations
    [[nodiscard]] awaitable_expected<inventory_ptr>
    fetch_locator_block_hashes(get_blocks_const_ptr locator, hash_digest const& threshold, size_t limit) const;

    [[nodiscard]] awaitable_expected<headers_ptr>
    fetch_locator_block_headers(get_headers_const_ptr locator, hash_digest const& threshold, size_t limit) const;

    [[nodiscard]] awaitable_expected<get_headers_ptr>
    fetch_block_locator(domain::chain::block::indexes const& heights) const;

    // Server queries
    [[nodiscard]] awaitable_expected<domain::chain::input_point>
    fetch_spend(domain::chain::output_point const& outpoint) const;

    [[nodiscard]] awaitable_expected<domain::chain::history_compact::list>
    fetch_history(short_hash const& address_hash, size_t limit, size_t from_height) const;

    [[nodiscard]] awaitable_expected<std::vector<hash_digest>>
    fetch_confirmed_transactions(short_hash const& address_hash, size_t limit, size_t from_height) const;

    [[nodiscard]] awaitable_expected<double_spend_proof_const_ptr>
    fetch_ds_proof(hash_digest const& hash) const;

    // =========================================================================
    // MEMPOOL / TRANSACTION POOL
    // =========================================================================

    [[nodiscard]] awaitable_expected<std::pair<merkle_block_ptr, size_t>>
    fetch_template() const;

    [[nodiscard]] awaitable_expected<inventory_ptr>
    fetch_mempool(size_t count_limit, uint64_t minimum_fee) const;

    std::vector<mempool_transaction_summary> get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const;
    std::vector<mempool_transaction_summary> get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const;
    std::vector<domain::chain::transaction> get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const;

    mempool_mini_hash_map get_mempool_mini_hash_map(domain::message::compact_block const& block) const;
    void fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const;

    // =========================================================================
    // FILTERS
    // =========================================================================

    [[nodiscard]] ::asio::awaitable<code>
    filter_blocks(get_data_ptr message) const;

    [[nodiscard]] ::asio::awaitable<code>
    filter_transactions(get_data_ptr message) const;

    // =========================================================================
    // ITERATION HELPERS
    // =========================================================================

    template <typename I, typename Handler>
    void for_each_tx_hash(I f, I l, size_t height, Handler handler) const {
        while (f != l) {
            auto const& hash = *f;
            auto const tx_result = database_.internal_db().get_transaction(hash, max_size_t);

            if ( ! tx_result) {
                handler(error::transaction_lookup_failed, 0, domain::chain::transaction{});
                return;
            }
            KTH_ASSERT(tx_result->height() == height);
            handler(error::success, height, tx_result->transaction());
            ++f;
        }
    }

    template <typename I, typename Handler>
    void for_each_tx_valid(I f, I l, size_t height, Handler handler) const {
        while (f != l) {
            auto const& tx = *f;
            if ( ! tx.is_valid()) {
                handler(error::transaction_lookup_failed, 0, domain::chain::transaction{});
                return;
            }
            handler(error::success, height, tx);
            ++f;
        }
    }

private:
    using handle = database::data_base::handle;

    template <typename R>
    void read_serial(R const& reader) const;

    template <typename Handler, typename... Args>
    bool finish_read(handle sequence, Handler handler, Args... args) const;

    code set_chain_state(domain::chain::chain_state::ptr previous);

    // Thread safe members
    std::atomic<bool> stopped_;
    settings const& settings_;
    time_t const notify_limit_seconds_;
    kth::atomic<block_const_ptr> last_block_;

    populate_chain_state const chain_state_populator_;
    database::data_base database_;

    // Protected by mutex
    domain::chain::chain_state::ptr pool_state_;
    mutable shared_mutex pool_state_mutex_;

    // Thread safe
    mutable prioritized_mutex validation_mutex_;
    mutable threadpool priority_pool_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool mempool_;
#endif

    transaction_organizer transaction_organizer_;
    block_organizer block_organizer_;
    header_index header_index_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP
