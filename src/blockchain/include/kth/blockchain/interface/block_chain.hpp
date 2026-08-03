// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP
#define KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <expected>
#include <mutex>
#include <optional>
#include <vector>

#include <kth/infrastructure/utility/atomic.hpp>

#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/database.hpp>
#include <kth/database/block_store.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/domain.hpp>
#include <kth/domain/chain/light_block.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/pools/block_organizer.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/pools/block_template.hpp>
#include <kth/blockchain/pools/mempool.hpp>
#include <kth/blockchain/pools/mempool_transaction_summary.hpp>
#include <kth/blockchain/pools/transaction_organizer.hpp>
#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/block_validation.hpp>
#include <kth/blockchain/validate/transaction_validation.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>


namespace kth::blockchain {

using kth::awaitable_expected;
using database::heights_t;

// One pooled transaction's own metadata (== BCHN getmempoolentry scalars; its
// depends / spentby are separate list queries).
struct mempool_entry_info {
    uint64_t fee;   // satoshis
    uint32_t size;  // serialized bytes
    uint64_t time;  // time first seen (unix seconds)
};

/// Unified blockchain interface.
/// Thread safety: get_* methods are NOT thread safe, fetch_* methods are thread safe.
struct KB_API block_chain {
    using executor_type = ::asio::any_io_executor;

    using mempool_mini_hash_map = boost::unordered_flat_map<mini_hash, domain::chain::transaction>;

    // =========================================================================
    // CONSTRUCTION
    // =========================================================================

    block_chain(blockchain::settings const& chain_settings,
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
    // THREAD POOL
    // =========================================================================

    [[nodiscard]] threadpool& thread_pool() { return priority_pool_; }

    // =========================================================================
    // LIFECYCLE
    // =========================================================================

    /// @param disk_magic Magic bytes for block files (e.g., 0xd9b4bef9 for BCH mainnet)
    [[nodiscard]] bool start(uint32_t disk_magic);
    [[nodiscard]] bool stop();
    [[nodiscard]] bool close();
    [[nodiscard]] bool stopped() const;

    // =========================================================================
    // ORGANIZERS (Core blockchain operations)
    // =========================================================================

    /// @param headers_pre_validated If true, skip header validation (for headers-first sync)
    [[nodiscard]]
    ::asio::awaitable<code> organize(block_const_ptr block, bool headers_pre_validated = false);

    /// Fast IBD: merkle validation (+ disk storage when enabled) for blocks under checkpoint.
    /// Posts work to priority_pool_ so network pool stays free for downloads.
    [[nodiscard]]
    ::asio::awaitable<code> organize_fast(std::shared_ptr<domain::chain::light_block const> block, size_t height);

    /// Fast IBD: parallel merkle validation for a chunk of light_blocks.
    /// Posts N merkle checks to priority_pool_ in parallel, awaits all results.
    [[nodiscard]]
    ::asio::awaitable<code> validate_chunk(
        std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
        uint32_t start_height);

    /// Fast IBD: store a validated light_block to flat files + update header_index + LMDB height.
    /// Posts disk I/O to priority_pool_ so the network executor stays free.
    [[nodiscard]]
    ::asio::awaitable<code> store_block(
        std::shared_ptr<domain::chain::light_block const> const& block,
        uint32_t height);

    /// Fast IBD: store an entire chunk of validated light_blocks in a single post to pool.
    /// Eliminates per-block post+await round-trip overhead (1 round-trip instead of N).
    [[nodiscard]]
    ::asio::awaitable<code> store_chunk(
        std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
        uint32_t start_height);

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
    ::asio::awaitable<code> push(transaction_const_ptr tx);

    [[nodiscard]] code push_sync(transaction_const_ptr tx);
    [[nodiscard]] bool insert(block_const_ptr block, size_t height);

#ifndef KTH_UTXOZ_COMPACT_MODE
    // Apply a batch of UTXO changes (full mode only — compact mode uses apply_utxo_delta_raw)
    template <database::utxo_insert_range Inserts, database::utxo_delete_range Deletes>
    [[nodiscard]]
    database::result_code apply_utxo_delta(Inserts const& inserts, Deletes const& deletes) {
        return utxoz_db_.apply_delta(inserts, deletes);
    }
#endif

    // Apply a batch of raw UTXO changes (zero-copy path, no domain objects)
    template <typename Inserts, typename Deletes>
    [[nodiscard]]
    database::result_code apply_utxo_delta_raw(Inserts const& inserts, Deletes const& deletes) {
        return utxoz_db_.apply_delta_raw(inserts, deletes);
    }

    // Set last block height in LMDB (for fast IBD storage progress tracking)
    [[nodiscard]]
    database::result_code set_last_block_height(uint32_t height);

    // Get/set the last block height for which UTXO set was built
    [[nodiscard]]
    std::expected<uint32_t, database::result_code> get_utxo_built_height() const;

    [[nodiscard]]
    database::result_code set_utxo_built_height(uint32_t height);

    // UTXO-Z maintenance operations
    [[nodiscard]]
    size_t utxo_deferred_deletions_size() const;

    [[nodiscard]]
    std::pair<size_t, std::vector<utxoz::deferred_deletion_entry>> utxo_process_pending_deletions();

    [[nodiscard]]
    size_t utxo_deferred_lookups_size() const;

    // Second phase of the find() contract: resolves outpoints whose find()
    // returned key_not_found (queued, not authoritative) by sweeping older file
    // versions. Returns {resolved by outpoint, keys that truly don't exist}.
    [[nodiscard]]
    std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, database::utxo_entry>, std::vector<utxoz::raw_outpoint>>
    utxo_process_pending_lookups();

    // =========================================================================
    // Reorg undo
    // =========================================================================

    // Read a UTXO's stored payload verbatim (no resolution). Same two-phase
    // contract as find(): key_not_found means "queued", not "absent" — resolve
    // with utxo_process_pending_lookups_raw().
    [[nodiscard]]
    std::expected<database::utxoz_database::raw_stored, database::result_code>
    find_utxo_raw(utxoz::raw_outpoint const& key, uint32_t height) const;

    // Raw counterpart of utxo_process_pending_lookups(): resolves the deferred
    // queue without reconstructing utxo_entry objects.
    [[nodiscard]]
    std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, database::utxoz_database::raw_stored>,
              std::vector<utxoz::raw_outpoint>>
    utxo_process_pending_lookups_raw();

#if ! defined(KTH_DB_READONLY)
    // Persist a block's undo data and record its position on the header index.
    // The undo record shares the block's file number, so the block must already
    // be stored (have_data) when this is called.
    [[nodiscard]]
    bool store_block_undo(database::header_index::index_t idx,
                          database::block_undo const& undo,
                          hash_digest const& prev_hash);
#endif

    // Read back a block's undo data, if any was recorded.
    [[nodiscard]]
    std::expected<database::block_undo, database::result_code>
    read_block_undo(database::header_index::index_t idx, hash_digest const& prev_hash) const;

#if ! defined(KTH_DB_READONLY)
    // Disconnect the block at `height` from the active chain, reverting its
    // effect on the UTXO set: the outputs it created are removed (recomputed by
    // re-reading the block) and the outputs it spent are restored from its undo
    // data, with their original creation heights.
    //
    // Only the UTXO set and the height markers are touched — the block data stays
    // on disk, so the block can be reconnected without re-downloading. `height`
    // must be the current validated tip: blocks are disconnected one at a time,
    // newest first.
    [[nodiscard]]
    database::disconnect_result disconnect_block(uint32_t height);

    // Switch the active chain onto `branch_head`, forking at `fork_height`.
    //
    // Disconnects the abandoned blocks newest-first (restoring their spent
    // outputs from undo data), then re-points the active chain at the new branch.
    // The caller is responsible for re-driving block download for the new branch:
    // its blocks are headers-only at this point, so the validated tip ends at
    // fork_height.
    //
    // Must be called with the UTXO build quiesced (see reorg_pause below): it
    // rewrites the UTXO set and the height markers.
    struct switch_result {
        bool ok{false};
        // Height the validated tip ended at, when it is known. On an aborted
        // disconnect this is how far it got — NOT where it started — so the caller
        // must resync from it. Empty means the tip could not be determined at all
        // (nothing was touched); the caller must then leave its counters alone
        // rather than treat it as height 0.
        std::optional<uint32_t> validated_tip;
    };

    [[nodiscard]]
    switch_result switch_to_branch(database::header_index::index_t branch_head, uint32_t fork_height);

    // Awaitable form of switch_to_branch: runs the disconnect on priority_pool_
    // instead of the caller's executor. A switch reads, parses and reverts every
    // abandoned block, so running it inline would stall the coordinator coroutine
    // and everything else sharing that executor for the whole reorg.
    [[nodiscard]]
    ::asio::awaitable<switch_result> switch_to_branch_async(
        database::header_index::index_t branch_head, uint32_t fork_height);
#endif

    // =========================================================================
    // Reorg barrier
    // =========================================================================
    //
    // A chain switch rewrites the UTXO set and the active chain while the block
    // pipeline (download -> validation -> storage -> UTXO build) is writing
    // against the chain being abandoned.
    //
    // A pause flag is checked by every participating task between units of work,
    // and a count tracks how many are parked. The switching side raises the pause
    // and waits until every registered participant is parked. A task that is
    // mid-unit finishes it first, so the barrier is only ever crossed at a unit
    // boundary. Unlike a pair of booleans this needs no store-load ordering
    // argument: the count only moves when a task is genuinely parked.
    //
    // Scope, stated plainly: this covers the tasks that write chain state
    // (block storage, UTXO build). The download and validation stages are NOT
    // drained yet — see the PR description.
    //
    // Participation is by explicit registration, not a hardcoded count: the
    // barrier is reached when every task that registered is parked. A task
    // deregisters when it exits, so a retired task neither blocks the barrier
    // forever nor counts as parked.

    void register_reorg_participant() { reorg_registered_.fetch_add(1, std::memory_order_seq_cst); }
    void unregister_reorg_participant() { reorg_registered_.fetch_sub(1, std::memory_order_seq_cst); }

    void request_reorg_pause(bool paused) { reorg_pause_.store(paused, std::memory_order_seq_cst); }
    [[nodiscard]] bool reorg_pause_requested() const { return reorg_pause_.load(std::memory_order_seq_cst); }

    // Called by a registered task when it parks at / leaves the barrier.
    void enter_reorg_barrier() { reorg_parked_.fetch_add(1, std::memory_order_seq_cst); }
    void leave_reorg_barrier() { reorg_parked_.fetch_sub(1, std::memory_order_seq_cst); }

    [[nodiscard]] bool reorg_barrier_reached() const {
        auto const registered = reorg_registered_.load(std::memory_order_seq_cst);
        // No participants: nothing writes the chain, so the barrier is trivially
        // satisfied (also the state after shutdown).
        return reorg_parked_.load(std::memory_order_seq_cst) >= registered;
    }

    void utxo_compact();
    void utxo_print_statistics();
    void utxo_print_sizing_report();
    void utxo_print_height_range_stats();

    // UTXO-Z iteration (for building bloom filter after IBD)
    template <typename F>
    void utxo_for_each(F&& callback) const {
        utxoz_db_.for_each_utxo(std::forward<F>(callback));
    }

    // UTXO-Z size (number of UTXOs in the set)
    [[nodiscard]]
    size_t utxo_size() const;

    // Set/clear bloom filter for skip-insert optimization
    void set_utxo_bloom(std::shared_ptr<database::utxo_bloom_filter const> bloom);
    void clear_utxo_bloom();
#endif

    // =========================================================================
    // CHAIN STATE
    // =========================================================================

    domain::chain::chain_state::ptr chain_state() const;
    domain::chain::chain_state::ptr chain_state(branch::const_ptr branch) const;

    /// Validator-owned side store for transient per-block validation state,
    /// keyed by block hash. This replaces the old mutable `block::validation`
    /// member so the domain value type carries only consensus/wire data.
    block_validation_store& block_validations() const;

    /// Validator-owned side store for transient per-transaction validation
    /// state, keyed by transaction hash. Replaces the old mutable
    /// `transaction::validation` member.
    transaction_validation_store& transaction_validations() const;

    /// The unconfirmed-transaction mempool. Owned here; the organizers admit to
    /// it (tx accept) and evict from it (block connect / reorg).
    blockchain::mempool& mempool_ref();
    blockchain::mempool const& mempool_ref() const;

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

    /// Data directory (parent of internal_db_dir, contains utxoz/, blocks/, etc.)
    [[nodiscard]]
    std::filesystem::path data_dir() const;

    /// Access the header index (for headers-first sync).
    [[nodiscard]] header_index& headers() { return header_index_; }
    [[nodiscard]] header_index const& headers() const { return header_index_; }


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

    [[nodiscard]] std::expected<output_info, database::result_code> get_utxo(
        domain::chain::output_point const& outpoint, size_t branch_height) const;

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

    // Batch fetch: single LMDB transaction for multiple blocks (optimized for UTXO building)
    [[nodiscard]] std::expected<domain::chain::block::list, database::result_code>
    fetch_blocks(uint32_t from, uint32_t to) const;

    // Raw batch fetch: returns serialized block data without deserialization
    [[nodiscard]] std::expected<std::vector<data_chunk>, database::result_code>
    fetch_blocks_raw(uint32_t from, uint32_t to) const;

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
    [[nodiscard]] awaitable_expected<double_spend_proof_const_ptr>
    fetch_ds_proof(hash_digest const& hash) const;

    // =========================================================================
    // MEMPOOL / TRANSACTION POOL
    // =========================================================================

    [[nodiscard]] awaitable_expected<blockchain::block_template>
    fetch_template() const;

    // Full mining template (header fields + coinbase value + tx selection) for
    // getblocktemplate[light]. C-API counterpart: kth_chain_async_fetch_mining_template.
    // coinbase_reserve_size is the space held back for the caller's coinbase; a
    // caller with extra coinbase outputs, such as an SV2 CoinbaseOutputDataSize
    // request, raises it above default_coinbase_reserve_size. The reserve is
    // part of the template cache key.
    [[nodiscard]] awaitable_expected<blockchain::mining_template>
    fetch_mining_template(uint64_t coinbase_reserve_size) const;

    // Mining-relevant chain snapshot (height, difficulty, mempool size, network)
    // for getmininginfo. C-API counterpart: kth_chain_async_fetch_mining_info.
    [[nodiscard]] awaitable_expected<blockchain::mining_info>
    fetch_mining_info() const;

    [[nodiscard]] awaitable_expected<inventory_ptr>
    fetch_mempool(size_t count_limit, uint64_t minimum_fee) const;

    std::vector<mempool_transaction_summary> get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const;
    std::vector<mempool_transaction_summary> get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const;
    std::vector<domain::chain::transaction> get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const;

    mempool_mini_hash_map get_mempool_mini_hash_map(domain::message::compact_block const& block) const;
    void fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const;

    // Mempool query readers, backing the typical BCH JSON-RPC mempool calls.
    hash_list get_mempool_txids() const;                                                             // getrawmempool
    blockchain::mempool_totals get_mempool_info() const;                                             // getmempoolinfo
    std::optional<mempool_entry_info> get_mempool_entry(hash_digest const& txid) const;              // getmempoolentry
    hash_list get_mempool_depends(hash_digest const& txid) const;                                    // getmempoolentry.depends
    hash_list get_mempool_spentby(hash_digest const& txid) const;                                    // getmempoolentry.spentby
    hash_list get_mempool_ancestors(hash_digest const& txid) const;                                  // getmempoolancestors
    hash_list get_mempool_descendants(hash_digest const& txid) const;                                // getmempooldescendants

    // Persist the mempool to <datadir>/mempool.dat (called on shutdown; also
    // backs a future savemempool). Returns false on I/O error.
    bool dump_mempool_to_disk() const;

    // Re-admit the persisted mempool through normal validation against the
    // current tip (called on startup, after the chain is up). Now-invalid /
    // confirmed / conflicting transactions are dropped. Returns the count admitted.
    [[nodiscard]] ::asio::awaitable<size_t> load_mempool_from_disk();

    // =========================================================================
    // FILTERS
    // =========================================================================

    [[nodiscard]] ::asio::awaitable<code>
    filter_blocks(get_data_ptr message) const;

    [[nodiscard]] ::asio::awaitable<code>
    filter_transactions(get_data_ptr message) const;

private:
    using handle = database::data_base::handle;

    template <typename R>
    void read_serial(R const& reader) const;

    template <typename Handler, typename... Args>
    bool finish_read(handle sequence, Handler handler, Args... args) const;

    code set_chain_state(domain::chain::chain_state::ptr previous);

    // <datadir>/mempool.dat — sibling of the blocks/ and utxoz/ directories.
    std::filesystem::path mempool_dat_path() const;

    // Thread safe members
    std::atomic<bool> stopped_;

    // Reorg barrier (see request_reorg_pause / enter_reorg_barrier above).
    std::atomic<bool> reorg_pause_{false};
    std::atomic<size_t> reorg_parked_{0};
    std::atomic<size_t> reorg_registered_{0};
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


    // Must be declared before block_organizer_: the organizer's block_pool is
    // constructed with a reference to this store.
    mutable block_validation_store block_validations_;
    mutable transaction_validation_store transaction_validations_;

    blockchain::mempool mempool_;

    // Block-template cache: fetch_mining_template() serves this while the tip
    // and mempool are unchanged (a mempool-only change within the refresh window
    // is also served). Lives in the core so both the JSON-RPC and C-API frontends
    // share it.
    //
    // Published as an immutable snapshot through an atomic shared_ptr: readers
    // load() it lock-free and serve a copy, so a GBT request never blocks on
    // another GBT request. The rebuild is coalesced by template_rebuild_mutex_
    // (try_lock), so at most one thread rebuilds; a concurrent caller whose
    // snapshot is stale only for the mempool (same tip) serves that snapshot
    // instead of blocking, while a tip change waits for the rebuild (a
    // wrong-parent template would orphan the miner's block). Insertion into the
    // mempool never touches either of these — it is a separate lock-free
    // structure.
    //
    // boost::atomic_shared_ptr (not std::atomic<std::shared_ptr>): the latter is
    // unsupported on macOS libc++.
    struct template_snapshot {
        blockchain::mining_template value;
        hash_digest previous;
        uint64_t generation;
        uint32_t time;
        uint64_t coinbase_reserve_size;
    };
    mutable boost::atomic_shared_ptr<template_snapshot> template_cache_;
    mutable std::mutex template_rebuild_mutex_;

    transaction_organizer transaction_organizer_;
    block_organizer block_organizer_;
    header_index header_index_;

    // Flat file block storage (for fast sequential I/O during IBD)
    std::unique_ptr<database::block_store> block_store_;

    // UTXO-Z high-performance UTXO database
    database::utxoz_database utxoz_db_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_BLOCK_CHAIN_HPP
