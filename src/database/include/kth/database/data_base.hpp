// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_DATA_BASE_HPP
#define KTH_DATABASE_DATA_BASE_HPP

#include <atomic>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>

#include <kth/domain.hpp>
#include <kth/database/define.hpp>
#include <kth/database/databases/internal_database.hpp>
#include <kth/database/define.hpp>
#include <kth/database/settings.hpp>
#include <kth/database/store.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

namespace kth::database {

using kth::awaitable_expected;

/// This class is thread safe and implements the sequential locking pattern.
struct KD_API data_base : store, noncopyable {
public:
    using executor_type = ::asio::any_io_executor;
    using handle = store::handle;
    using path = kth::path;

    // Construct.
    // ----------------------------------------------------------------------------

    data_base(settings const& settings);

    // Open and close.
    // ------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)
    /// Create and open all databases.
    bool create(domain::chain::block const& genesis);
#endif

    /// Open all databases.
    bool open();

    /// Close all databases.
    bool close();

    /// Call close on destruct.
    ~data_base();

    // Readers.
    // ------------------------------------------------------------------------

    internal_database const& internal_db() const;

    // Synchronous writers.
    // ------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)


    /// Store a block in the database.
    /// Returns store_block_duplicate if a block already exists at height.
    code insert(domain::chain::block const& block, size_t height);

    /// Add an unconfirmed tx to the store (without indexing).
    /// Returns unspent_duplicate if existing unspent hash duplicate exists.
    code push(domain::chain::transaction const& tx, uint32_t forks);

    /// Returns store_block_missing_parent if not linked.
    /// Returns store_block_invalid_height if height is not the current top + 1.
    code push(domain::chain::block const& block, size_t height);

    /// Fast IBD: store only block data without UTXO updates (for blocks under checkpoint).
    /// Much faster than push() but requires a second pass to build UTXO set.
    code push_block_fast(domain::chain::block const& block, size_t height);

    /// Push a header for headers-first sync (without full block data).
    /// Returns store_block_invalid_height if height is not the current top + 1.
    code push_header(domain::chain::header const& header, size_t height);

    /// Push a header with explicit ABLA state for headers-first sync.
    /// Returns store_block_invalid_height if height is not the current top + 1.
    code push_header(domain::chain::header const& header, size_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size);

    /// Push multiple headers in a single transaction (batch).
    /// start_height is the height of the first header in the list.
    code push_headers_batch(domain::chain::header::list const& headers, size_t start_height);

    code prune_reorg();

    //bool set_database_flags(bool fast);

    // Coroutine writers.
    // ------------------------------------------------------------------------

    /// Invoke pop_above and then push_all.
    /// Returns the outgoing blocks that were popped.
    [[nodiscard]]
    awaitable_expected<block_const_ptr_list_ptr> reorganize(
        executor_type executor,
        infrastructure::config::checkpoint const& fork_point,
        block_const_ptr_list_const_ptr incoming_blocks);
#endif // ! defined(KTH_DB_READONLY)

protected:
    void start();

#if ! defined(KTH_DB_READONLY)

    // Push all blocks starting at first_height.
    // Selects parallel or sequential based on settings_.parallel_block_push.
    [[nodiscard]]
    ::asio::awaitable<code> push_all(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height);

    // Pop the set of blocks above the given hash.
    // Returns the popped blocks or an error if the database is corrupt or the hash doesn't exist.
    [[nodiscard]]
    awaitable_expected<block_const_ptr_list_ptr> pop_above(executor_type executor, hash_digest const& fork_hash);

#endif // ! defined(KTH_DB_READONLY)

    std::shared_ptr<internal_database> internal_db_;

private:
    using inputs = domain::chain::input::list;
    using outputs = domain::chain::output::list;

#if ! defined(KTH_DB_READONLY)
    code push_genesis(domain::chain::block const& block);

    // Synchronous helpers.
    // ------------------------------------------------------------------------
    bool pop(domain::chain::block& out_block);
    bool pop_inputs(inputs const& inputs, size_t height);
    bool pop_outputs(outputs const& outputs, size_t height);

    // Push a single block to the database (synchronous, called from coroutine).
    code do_push(block_const_ptr block, size_t height, uint32_t median_time_past);

    // Push all blocks sequentially (one after another).
    [[nodiscard]]
    ::asio::awaitable<code> push_all_sequential(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height);

    // Push all blocks in parallel (dispatch all, collect results).
    [[nodiscard]]
    ::asio::awaitable<code> push_all_parallel(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height);
#endif // ! defined(KTH_DB_READONLY)

    code verify_insert(domain::chain::block const& block, size_t height);
    code verify_push(domain::chain::block const& block, size_t height) const;

    std::atomic<bool> closed_;
    settings const& settings_;
};

} // namespace kth::database

#endif
