// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/data_base.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/range/adaptor/reversed.hpp>

#include <kth/database/define.hpp>
#include <kth/database/settings.hpp>
#include <kth/database/store.hpp>
#include <kth/domain.hpp>

namespace kth::database {

using namespace kth::domain::chain;
using namespace kth::config;
using namespace kth::domain::wallet;
using namespace boost::adaptors;
using namespace std::filesystem;
using namespace std::placeholders;

#define NAME "data_base"

// A failure after begin_write is returned without calling end_write.
// This purposely leaves the local flush lock (as enabled) and inverts the
// sequence lock. The former prevents usagage after restart and the latter
// prevents continuation of reads and writes while running. Ideally we
// want to exit without clearing the global flush lock (as enabled).

// Construct.
// ----------------------------------------------------------------------------

data_base::data_base(settings const& settings)
    : closed_(true)
    , settings_(settings)
    , store(settings.directory)
{}

data_base::~data_base() {
    close();
}

// Open and close.
// ----------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)
// Throws if there is insufficient disk space, not idempotent.
bool data_base::create(block const& genesis) {
    start();

    // These leave the databases open.
    auto created = true
#if ! defined(KTH_DB_READONLY)
                && internal_db_->create()
#endif
    ;

    if ( ! created) {
        return false;
    }

    // Store the first block.
    // push(genesis, 0);
    push_genesis(genesis);

    closed_ = false;
    return true;
}
#endif // ! defined(KTH_DB_READONLY)

// Must be called before performing queries, not idempotent.
// May be called after stop and/or after close in order to reopen.
bool data_base::open() {
    start();
    auto const opened = internal_db_->open();
    closed_ = false;
    return opened;
}

// Close is idempotent and thread safe.
// Optional as the database will close on destruct.
bool data_base::close() {
    if (closed_) {
        return true;
    }

    closed_ = true;
    auto const closed = internal_db_->close();
    return closed;
}

// protected
void data_base::start() {
    internal_db_ = std::make_shared<internal_database>(
        internal_db_dir,
        settings_.db_mode,
        settings_.reorg_pool_limit,
        settings_.db_max_size, settings_.safe_mode);
}

// Readers.
// ----------------------------------------------------------------------------

internal_database const& data_base::internal_db() const {
    return *internal_db_;
}

// Synchronous writers.
// ----------------------------------------------------------------------------

static inline
uint32_t get_next_height(internal_database const& db) {
    uint32_t current_height;
    auto res = db.get_last_height(current_height);

    if (res != result_code::success) {
        return 0;
    }

    return current_height + 1;
}

static inline
hash_digest get_previous_hash(internal_database const& db, size_t height) {
    return height == 0 ? null_hash : db.get_header(height - 1).hash();
}

//TODO(fernando): const?
code data_base::verify_insert(block const& block, size_t height) {
    if (block.transactions().empty()) {
        return error::empty_block;
    }

    auto res = internal_db_->get_header(height);

    if (res.is_valid()) {
        return error::store_block_duplicate;
    }

    return error::success;
}

code data_base::verify_push(block const& block, size_t height) const {
    if (block.transactions().empty()) {
        return error::empty_block;
    }

    if (settings_.db_mode == db_mode_type::pruned) {
        return error::success;
    }

    if (get_next_height(internal_db()) != height) {
        return error::store_block_invalid_height;
    }

    if (block.header().previous_block_hash() != get_previous_hash(internal_db(), height)) {
        return error::store_block_missing_parent;
    }

    return error::success;
}


#if ! defined(KTH_DB_READONLY)

// Add block to the database at the given height (gaps allowed/created).
// This is designed for write concurrency but only with itself.
code data_base::insert(domain::chain::block const& block, size_t height) {

    auto const median_time_past = block.header().validation.median_time_past;

    auto const ec = verify_insert(block, height);

    if (ec) return ec;

    auto res = internal_db_->push_block(block, height, median_time_past);
    if ( ! succeed(res)) {
        return error::database_insert_failed;   //TODO(fernando): create a new operation_failed
    }

    return error::success;
}
#endif //! defined(KTH_DB_READONLY)

#if ! defined(KTH_DB_READONLY)

// This is designed for write exclusivity and read concurrency.
code data_base::push(domain::chain::transaction const& tx, uint32_t forks) {
    if (settings_.db_mode != db_mode_type::full) {
        return error::success;
    }

    //We insert only in transaction unconfirmed here
    internal_db_->push_transaction_unconfirmed(tx, forks);
    return error::success;  //TODO(fernando): store the transactions in a new mempool
}

#endif // ! defined(KTH_DB_READONLY)

#if ! defined(KTH_DB_READONLY)
// Add a block in order (creates no gaps, must be at top).
// This is designed for write exclusivity and read concurrency.
code data_base::push(block const& block, size_t height) {
    auto const median_time_past = block.header().validation.median_time_past;
    auto res = internal_db_->push_block(block, height, median_time_past);
    if ( ! succeed(res)) {
        return error::database_push_failed;   //TODO(fernando): create a new operation_failed
    }
    return error::success;
}

// private
// Add the Genesis block
code data_base::push_genesis(block const& block) {
    auto res = internal_db_->push_genesis(block);
    if ( ! succeed(res)) {
        return error::database_push_failed;   //TODO(fernando): create a new operation_failed
    }

    return error::success;
}
#endif // ! defined(KTH_DB_READONLY)



#if defined(KTH_CURRENCY_BCH)

#if ! defined(KTH_DB_READONLY)
// A false return implies store corruption.
bool data_base::pop(block& out_block) {

    auto const start_time = asio::steady_clock::now();

    if (internal_db_->pop_block(out_block) != result_code::success) {
        return false;
    }

    out_block.validation.error = error::success;
    out_block.validation.start_pop = start_time;
    return true;
}
#endif // ! defined(KTH_DB_READONLY)

#else // KTH_CURRENCY_BCH

#if ! defined(KTH_DB_READONLY)
// A false return implies store corruption.
bool data_base::pop(block& out_block) {

    auto const start_time = asio::steady_clock::now();

    if (internal_db_->pop_block(out_block) != result_code::success) {
        return false;
    }

    out_block.validation.error = error::success;
    out_block.validation.start_pop = start_time;
    return true;
}
#endif //! defined(KTH_DB_READONLY)
#endif // KTH_CURRENCY_BCH


#if ! defined(KTH_DB_READONLY)
// A false return implies store corruption.
bool data_base::pop_inputs(const input::list& inputs, size_t height) {

    return true;
}

// A false return implies store corruption.
bool data_base::pop_outputs(const output::list& outputs, size_t height) {
    return true;
}
#endif //! defined(KTH_DB_READONLY)

// Asynchronous writers.
// ----------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)
// Add a list of blocks in order.
// If the dispatch threadpool is shut down when this is running the handler
// will never be invoked, resulting in a threadpool.join indefinite hang.
void data_base::push_all(block_const_ptr_list_const_ptr in_blocks, size_t first_height, dispatcher& dispatch, result_handler handler) {
    DEBUG_ONLY(*safe_add(in_blocks->size(), first_height));

    // This is the beginning of the push_all sequence.
    push_next(error::success, in_blocks, 0, first_height, dispatch, handler);
}

// TODO(legacy): resolve inconsistency with height and median_time_past passing.
void data_base::push_next(code const& ec, block_const_ptr_list_const_ptr blocks, size_t index, size_t height, dispatcher& dispatch, result_handler handler) {
    if (ec || index >= blocks->size()) {
        // This ends the loop.
        handler(ec);
        return;
    }

    auto const block = (*blocks)[index];
    auto const median_time_past = block->header().validation.median_time_past;

    // Set push start time for the block.
    block->validation.start_push = asio::steady_clock::now();

    result_handler const next = std::bind(&data_base::push_next, this, _1, blocks, index + 1, height + 1, std::ref(dispatch), handler);

    // This is the beginning of the block sub-sequence.
    dispatch.concurrent(&data_base::do_push, this, block, height, median_time_past, std::ref(dispatch), next);
}

void data_base::do_push(block_const_ptr block, size_t height, uint32_t median_time_past, dispatcher& dispatch, result_handler handler) {
    // spdlog::debug("[database] Write flushed to disk: {}", ec.message());
    auto res = internal_db_->push_block(*block, height, median_time_past);
    if ( ! succeed(res)) {
        handler(error::database_concurrent_push_failed); //TODO(fernando): create a new operation_failed
        return;
    }
    block->validation.end_push = asio::steady_clock::now();
    // This is the end of the block sub-sequence.
    handler(error::success);
}

// TODO(legacy): make async and concurrency as appropriate.
// This precludes popping the genesis block.
void data_base::pop_above(block_const_ptr_list_ptr out_blocks, hash_digest const& fork_hash, dispatcher&, result_handler handler) {
    out_blocks->clear();

    auto const header_result = internal_db_->get_header(fork_hash);

    uint32_t top;
    // The fork point does not exist or failed to get it or the top, fail.
    if ( ! header_result.first.is_valid() ||  internal_db_->get_last_height(top) != result_code::success) {
        //**--**
        handler(error::chain_reorganization_failed);
        return;
    }

    auto const fork = header_result.second;

    auto const size = top - fork;

    // The fork is at the top of the chain, nothing to pop.
    if (size == 0) {
        handler(error::success);
        return;
    }

    // If the fork is at the top there is one block to pop, and so on.
    out_blocks->reserve(size);

    // Enqueue blocks so .front() is fork + 1 and .back() is top.
    for (size_t height = top; height > fork; --height) {
        domain::message::block next;

        // TODO(legacy): parallelize pop of transactions within each block.
        if ( ! pop(next)) {
            //**--**
            handler(error::database_insert_failed);
            return;
        }

        KTH_ASSERT(next.is_valid());
        auto block = std::make_shared<domain::message::block const>(std::move(next));
        out_blocks->insert(out_blocks->begin(), block);
    }

    handler(error::success);
}

code data_base::prune_reorg() {
    auto res = internal_db_->prune();
    if ( ! succeed_prune(res)) {
        spdlog::error("[database] Error pruning the reorganization pool, code: {}", static_cast<std::underlying_type<result_code>::type>(res));
        return error::unknown;
    }
    return error::success;
}
#endif // ! defined(KTH_DB_READONLY)

#if ! defined(KTH_DB_READONLY)
// This is designed for write exclusivity and read concurrency.
void data_base::reorganize(infrastructure::config::checkpoint const& fork_point, block_const_ptr_list_const_ptr incoming_blocks, block_const_ptr_list_ptr outgoing_blocks, dispatcher& dispatch, result_handler handler) {
    auto const next_height = *safe_add(fork_point.height(), size_t(1));
    // TODO: remove std::bind, use lambda instead.
    // TOOD: Even better use C++20 coroutines.
    result_handler const pop_handler = std::bind(&data_base::handle_pop, this, _1, incoming_blocks, next_height, std::ref(dispatch), handler);
    pop_above(outgoing_blocks, fork_point.hash(), dispatch, pop_handler);
}

void data_base::handle_pop(code const& ec, block_const_ptr_list_const_ptr incoming_blocks, size_t first_height, dispatcher& dispatch, result_handler handler) {
    result_handler const push_handler = std::bind(&data_base::handle_push, this, _1, handler);

    if (ec) {
        push_handler(ec);
        return;
    }

    push_all(incoming_blocks, first_height, std::ref(dispatch), push_handler);
}

// We never invoke the caller's handler under the mutex, we never fail to clear
// the mutex, and we always invoke the caller's handler exactly once.
void data_base::handle_push(code const& ec, result_handler handler) const {
    if (ec) {
        handler(ec);
        return;
    }
    handler(error::success);
}
#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database