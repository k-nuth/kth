// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_DATA_BASE_HPP
#define KTH_DATABASE_DATA_BASE_HPP

#include <atomic>
#include <cstddef>
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
#include <kth/infrastructure/utility/dispatcher.hpp>

namespace kth::database {

/// This class is thread safe and implements the sequential locking pattern.
struct KD_API data_base : store, noncopyable {
public:
    using handle = store::handle;
    using result_handler = handle0;
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

    code prune_reorg();

    //bool set_database_flags(bool fast);

    // Asynchronous writers.
    // ------------------------------------------------------------------------

    /// Invoke pop_all and then push_all under a common lock.
    void reorganize(infrastructure::config::checkpoint const& fork_point, block_const_ptr_list_const_ptr incoming_blocks, block_const_ptr_list_ptr outgoing_blocks, dispatcher& dispatch, result_handler handler);
#endif // ! defined(KTH_DB_READONLY)

protected:
    void start();

#if ! defined(KTH_DB_READONLY)

    // Sets error if first_height is not the current top + 1 or not linked.
    void push_all(block_const_ptr_list_const_ptr in_blocks, size_t first_height, dispatcher& dispatch, result_handler handler);

    // Pop the set of blocks above the given hash.
    // Sets error if the database is corrupt or the hash doesn't exist.
    // Any blocks returned were successfully popped prior to any failure.
    void pop_above(block_const_ptr_list_ptr out_blocks, hash_digest const& fork_hash, dispatcher& dispatch, result_handler handler);
#endif // ! defined(KTH_DB_READONLY)


    std::shared_ptr<internal_database> internal_db_;

private:
    using inputs = domain::chain::input::list;
    using outputs = domain::chain::output::list;

#if ! defined(KTH_DB_READONLY)
    code push_genesis(domain::chain::block const& block);

    // Synchronous writers.
    // ------------------------------------------------------------------------
    bool pop(domain::chain::block& out_block);
    bool pop_inputs(const inputs& inputs, size_t height);
    bool pop_outputs(const outputs& outputs, size_t height);

#endif // ! defined(KTH_DB_READONLY)

    code verify_insert(domain::chain::block const& block, size_t height);
    code verify_push(domain::chain::block const& block, size_t height) const;

    // Asynchronous writers.
    // ------------------------------------------------------------------------
#if ! defined(KTH_DB_READONLY)
    void push_next(code const& ec, block_const_ptr_list_const_ptr blocks, size_t index, size_t height, dispatcher& dispatch, result_handler handler);
    void do_push(block_const_ptr block, size_t height, uint32_t median_time_past, dispatcher& dispatch, result_handler handler);


    void handle_pop(code const& ec, block_const_ptr_list_const_ptr incoming_blocks, size_t first_height, dispatcher& dispatch, result_handler handler);
    void handle_push(code const& ec, result_handler handler) const;
#endif // ! defined(KTH_DB_READONLY)

    std::atomic<bool> closed_;
    settings const& settings_;
};

} // namespace kth::database

#endif
