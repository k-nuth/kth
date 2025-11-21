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
#include <vector>

// #include <kth/infrastructure.hpp>
#include <kth/infrastructure/utility/atomic.hpp>

#include <kth/database.hpp>
#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/blockchain/pools/block_organizer.hpp>
#include <kth/blockchain/pools/transaction_organizer.hpp>
#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

/// The fast_chain interface portion of this class is not thread safe.
struct KB_API block_chain : safe_chain, fast_chain, noncopyable {
public:
    /// Relay transactions is network setting that is passed through to block
    /// population as an optimization. This can be removed once there is an
    /// in-memory cache of tx pool metadata, as the costly query will go away.
    block_chain(threadpool& pool
        , blockchain::settings const& chain_settings
        , database::settings const& database_settings
        , domain::config::network network
        , bool relay_transactions = true);

    /// The database is closed on destruct, threads must be joined.
    ~block_chain();

    // ========================================================================
    // FAST CHAIN
    // ========================================================================

    // Readers.
    // ------------------------------------------------------------------------
    // Thread safe, unprotected by sequential lock.

    /// Get the output that is referenced by the outpoint.
    bool get_output(domain::chain::output& out_output, size_t& out_height, uint32_t& out_median_time_past, bool& out_coinbase, const domain::chain::output_point& outpoint, size_t branch_height, bool require_confirmed) const override;

    /// Get position data for a transaction.
    bool get_transaction_position(size_t& out_height, size_t& out_position, hash_digest const& hash, bool require_confirmed) const override;

    /// Get the output that is referenced by the outpoint in the UTXO Set.
    bool get_utxo(domain::chain::output& out_output, size_t& out_height, uint32_t& out_median_time_past, bool& out_coinbase, domain::chain::output_point const& outpoint, size_t branch_height) const override;

    std::pair<bool, database::internal_database::utxo_pool_t> get_utxo_pool_from(uint32_t from, uint32_t to) const override;

    /// Get a determination of whether the block hash exists in the store.
    bool get_block_exists(hash_digest const& block_hash) const override;

    /// Get a determination of whether the block hash exists in the store.
    bool get_block_exists_safe(hash_digest const& block_hash) const override;

    /// Get the work of the branch starting at the given height.
    bool get_branch_work(uint256_t& out_work, uint256_t const& maximum, size_t height) const override;

    /// Get the header of the block at the given height.
    bool get_header(domain::chain::header& out_header, size_t height) const override;

    /// Get the header of the block with the given height, also the ABLA state.
    std::optional<database::header_with_abla_state_t> get_header_and_abla_state(size_t height) const override;

    /// Get a sequence of block headers [from, to].
    domain::chain::header::list get_headers(size_t from, size_t to) const override;

    /// Get the height of the block with the given hash.
    bool get_height(size_t& out_height, hash_digest const& block_hash) const override;

    /// Get the bits of the block with the given height.
    bool get_bits(uint32_t& out_bits, size_t height) const override;

    /// Get the timestamp of the block with the given height.
    bool get_timestamp(uint32_t& out_timestamp, size_t height) const override;

    /// Get the version of the block with the given height.
    bool get_version(uint32_t& out_version, size_t height) const override;

    /// Get height of latest block.
    bool get_last_height(size_t& out_height) const override;

#if ! defined(KTH_DB_READONLY)
    void prune_reorg_async() override;
#endif

    //void set_database_flags() override;

    /// Get the hash of the block if it exists.
    bool get_block_hash(hash_digest& out_hash, size_t height) const override;


    /////// Get the transaction of the given hash and its block height.
    ////transaction_ptr get_transaction(size_t& out_block_height,
    ////    hash_digest const& hash, bool require_confirmed) const;

    // Writers.
    // ------------------------------------------------------------------------
    // Thread safe, insert does not set sequential lock.

#if ! defined(KTH_DB_READONLY)

    /// Insert a block to the blockchain, height is checked for existence.
    /// Reads and reorgs are undefined when chain is gapped.
    bool insert(block_const_ptr block, size_t height) override;
    // bool insert(block_const_ptr block, size_t height, int) override;

    /// Push an unconfirmed transaction to the tx table and index outputs.
    void push(transaction_const_ptr tx, dispatcher& dispatch, result_handler handler) override;

    /// Swap incoming and outgoing blocks, height is validated.
    void reorganize(const infrastructure::config::checkpoint& fork_point,
        block_const_ptr_list_const_ptr incoming_blocks,
        block_const_ptr_list_ptr outgoing_blocks, dispatcher& dispatch,
        result_handler handler) override;

#endif // ! defined(KTH_DB_READONLY)

    // Properties
    // ------------------------------------------------------------------------

    /// Get forks chain state relative to chain top.
    domain::chain::chain_state::ptr chain_state() const override;

    /// Get full chain state relative to the branch top.
    domain::chain::chain_state::ptr chain_state(branch::const_ptr branch) const override;

    // ========================================================================
    // SAFE CHAIN
    // ========================================================================
    // Thread safe.

    // Startup and shutdown.
    // ------------------------------------------------------------------------
    // Thread safe except start.

    /// Start the block pool and the transaction pool.
    bool start() override;

    /// Signal pool work stop, speeds shutdown with multiple threads.
    bool stop() override;

    /// Unmaps all memory and frees the database file handles.
    /// Threads must be joined before close is called (or by destruct).
    bool close() override;

    // Node Queries.
    // ------------------------------------------------------------------------

    /// fetch a block by height.
    void fetch_block(size_t height, block_fetch_handler handler) const override;

    /// fetch a block by hash.
    void fetch_block(hash_digest const& hash, block_fetch_handler handler) const override;

    /// fetch the set of block hashes indicated by the block locator.
    void fetch_locator_block_hashes(get_blocks_const_ptr locator, hash_digest const& threshold, size_t limit, inventory_fetch_handler handler) const override;

    void fetch_block_header_txs_size(hash_digest const& hash, block_header_txs_size_fetch_handler handler) const override;

    /// fetch hashes of transactions for a block, by block height.
    void fetch_merkle_block(size_t height, merkle_block_fetch_handler handler) const override;

    /// fetch hashes of transactions for a block, by block hash.
    void fetch_merkle_block(hash_digest const& hash, merkle_block_fetch_handler handler) const override;

    /// fetch compact block by block height.
    void fetch_compact_block(size_t height, compact_block_fetch_handler handler) const override;

    /// fetch compact block by block hash.
    void fetch_compact_block(hash_digest const& hash, compact_block_fetch_handler handler) const override;

    /// fetch DSProof by hash.
    void fetch_ds_proof(hash_digest const& hash, ds_proof_fetch_handler handler) const override;

    // void for_each_transaction(size_t from, size_t to, for_each_tx_handler const& handler) const override;

    // void for_each_transaction_non_coinbase(size_t from, size_t to, for_each_tx_handler const& handler) const override;

    /// fetch transaction by hash.
    void fetch_transaction(hash_digest const& hash, bool require_confirmed, transaction_fetch_handler handler) const override;

    /// fetch position and height within block of transaction by hash.
    void fetch_transaction_position(hash_digest const& hash, bool require_confirmed, transaction_index_fetch_handler handler) const override;

    /// fetch the set of block headers indicated by the block locator.
    void fetch_locator_block_headers(get_headers_const_ptr locator, hash_digest const& threshold, size_t limit, locator_block_headers_fetch_handler handler) const override;

    /// fetch a block locator relative to the current top and threshold.
    void fetch_block_locator(domain::chain::block::indexes const& heights, block_locator_fetch_handler handler) const override;

    /// fetch height of latest block.
    void fetch_last_height(last_height_fetch_handler handler) const override;

        /// fetch block header by height.
    void fetch_block_header(size_t height, block_header_fetch_handler handler) const override;

    /// fetch block header by hash.
    void fetch_block_header(hash_digest const& hash, block_header_fetch_handler handler) const override;

    /// fetch height of block by hash.
    void fetch_block_height(hash_digest const& hash, block_height_fetch_handler handler) const override;

    void fetch_block_hash_timestamp(size_t height, block_hash_time_fetch_handler handler) const override;

    // Knuth non-virtual functions.
    //-------------------------------------------------------------------------

    template <typename I>
    void for_each_tx_hash(I f, I l, size_t height, for_each_tx_handler handler) const {
        while (f != l) {
            auto const& hash = *f;
            auto const tx_result = database_.internal_db().get_transaction(hash, max_size_t);

            if ( ! tx_result.is_valid()) {
                handler(error::transaction_lookup_failed, 0, domain::chain::transaction{});
                return;
            }
            KTH_ASSERT(tx_result.height() == height);
            handler(error::success, height, tx_result.transaction());
            ++f;
        }
    }

    template <typename I>
    void for_each_tx_valid(I f, I l, size_t height, for_each_tx_handler handler) const {
        while (f != l) {
            auto const& tx = *f;

            if ( ! tx.is_valid()) {
                handler(error::transaction_lookup_failed, 0, domain::chain::transaction{});
                return;
            }
            //KTH_ASSERT(tx.height() == height);
            handler(error::success, height, tx);
            ++f;
        }
    }

    // Server Queries.
    //-------------------------------------------------------------------------

    /// fetch the inpoint (spender) of an outpoint.
    void fetch_spend(const domain::chain::output_point& outpoint, spend_fetch_handler handler) const override;
    /// fetch outputs, values and spends for an address_hash.
    void fetch_history(const short_hash& address_hash, size_t limit, size_t from_height, history_fetch_handler handler) const override;

    /// Fetch all the txns used by the wallet
    void fetch_confirmed_transactions(const short_hash& address_hash, size_t limit, size_t from_height, confirmed_transactions_fetch_handler handler) const override;

//     /// fetch stealth results.
//     void fetch_stealth(const binary& filter, size_t from_height, stealth_fetch_handler handler) const override;

    // Transaction Pool.
    //-------------------------------------------------------------------------

    /// Fetch a merkle block for the maximal fee block template.
    void fetch_template(merkle_block_fetch_handler handler) const override;

    /// Fetch an inventory vector for a rational "mempool" message response.
    void fetch_mempool(size_t count_limit, uint64_t minimum_fee, inventory_fetch_handler handler) const override;


    std::vector<mempool_transaction_summary> get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const override;
    std::vector<mempool_transaction_summary> get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const override;
    std::vector<domain::chain::transaction> get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const override;

    /// fetch unconfirmed transaction by hash.
    void fetch_unconfirmed_transaction(hash_digest const& hash, transaction_unconfirmed_fetch_handler handler) const override;

    mempool_mini_hash_map get_mempool_mini_hash_map(domain::message::compact_block const& block) const override;
    void fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const override;

    // Filters.
    //-------------------------------------------------------------------------

    /// Filter out block by hash that exist in the block pool or store.
    void filter_blocks(get_data_ptr message, result_handler handler) const override;

    /// Filter out confirmed and unconfirmed transactions by hash.
    void filter_transactions(get_data_ptr message, result_handler handler) const override;

    // Subscribers.
    //-------------------------------------------------------------------------

    /// Subscribe to blockchain reorganizations, get branch/height.
    void subscribe_blockchain(reorganize_handler&& handler) override;

    /// Subscribe to memory pool additions, get transaction.
    void subscribe_transaction(transaction_handler&& handler) override;

    /// Subscribe to DSProof pool additions, get DSProof object.
    void subscribe_ds_proof(ds_proof_handler&& handler) override;

    /// Send null data success notification to all subscribers.
    void unsubscribe() override;

    // Transaction Validation.
    //-----------------------------------------------------------------------------

    void transaction_validate(transaction_const_ptr tx, result_handler handler) const override;

    // Organizers.
    //-------------------------------------------------------------------------

    /// Organize a block into the block pool if valid and sufficient.
    void organize(block_const_ptr block, result_handler handler) override;

    /// Store a transaction to the pool if valid.
    void organize(transaction_const_ptr tx, result_handler handler) override;

    /// Store a DSProof to the pool if valid.
    void organize(double_spend_proof_const_ptr ds_proof, result_handler handler) override;

    // Properties.
    //-------------------------------------------------------------------------

    /// True if the blockchain is stale based on configured age limit.
    bool is_stale() const override;

    /// True if the blockchain is stale based on configured age limit.
    bool is_stale_fast() const override;

    /// Get a reference to the blockchain configuration settings.
    settings const& chain_settings() const;

#if defined(KTH_WITH_MEMPOOL)
    std::pair<std::vector<kth::mining::transaction_element>, uint64_t> get_block_template() const;
#endif

protected:

    /// Determine if work should terminate early with service stopped code.
    bool stopped() const;

private:
    using handle = database::data_base::handle;

    // Locking helpers.
    // ------------------------------------------------------------------------

    template <typename R>
    void read_serial(R const& reader) const;

    template <typename Handler, typename... Args>
    bool finish_read(handle sequence, Handler handler, Args... args) const;

    // Utilities.
    //-------------------------------------------------------------------------

    code set_chain_state(domain::chain::chain_state::ptr previous);
    void handle_transaction(code const& ec, transaction_const_ptr tx, result_handler handler) const;
    void handle_block(code const& ec, block_const_ptr block, result_handler handler) const;
    void handle_reorganize(code const& ec, block_const_ptr top, result_handler handler);

    // These are thread safe.
    std::atomic<bool> stopped_;
    settings const& settings_;
    const time_t notify_limit_seconds_;
    kth::atomic<block_const_ptr> last_block_;

    //TODO(kth):  dissabled this tx cache because we don't want special treatment for the last txn, it affects the explorer rpc methods
    //kth::atomic<transaction_const_ptr> last_transaction_;

    populate_chain_state const chain_state_populator_;
    database::data_base database_;

    // This is protected by mutex.
    domain::chain::chain_state::ptr pool_state_;
    mutable shared_mutex pool_state_mutex_;

    // These are thread safe.
    mutable prioritized_mutex validation_mutex_;
    mutable threadpool priority_pool_;
    mutable dispatcher dispatch_;


#if defined(KTH_WITH_MEMPOOL)
    mining::mempool mempool_;
#endif

    transaction_organizer transaction_organizer_;
    block_organizer block_organizer_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP
