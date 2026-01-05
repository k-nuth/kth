// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/data_base.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/range/adaptor/reversed.hpp>

#include <kth/database/define.hpp>
#include <kth/database/settings.hpp>
#include <kth/database/store.hpp>
#include <kth/domain.hpp>

#include <asio/co_spawn.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::database {

using namespace kth::domain::chain;
using namespace kth::config;
using namespace kth::domain::wallet;
using namespace boost::adaptors;
using namespace std::filesystem;

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
    auto res = push_genesis(genesis);
    if (res != error::success) {
        return false;
    }

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
    return internal_db_->close();
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
uint32_t get_next_header_height(internal_database const& db) {
    auto result = db.get_last_heights();
    if ( ! result) {
        return 0;
    }
    return result->header + 1;
}

static inline
std::optional<hash_digest> get_previous_hash(internal_database const& db, size_t height) {
    if (height == 0) {
        return std::nullopt;  // Genesis block has no previous block
    }
    auto result = db.get_header(height - 1);
    if ( ! result) {
        return std::nullopt;
    }
    return result->hash();
}

//TODO(fernando): const?
code data_base::verify_insert(block const& block, size_t height) {
    if (block.transactions().empty()) {
        return error::empty_block;
    }

    auto res = internal_db_->get_header(height);

    if (res) {
        // Header already exists at this height
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

    if (get_next_header_height(internal_db()) != height) {
        return error::store_block_invalid_height;
    }

    auto const prev_hash = get_previous_hash(internal_db(), height);
    if ( ! prev_hash || block.header().previous_block_hash() != *prev_hash) {
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

// Add a header for headers-first sync (without full block data).
// This is designed for write exclusivity and read concurrency.
code data_base::push_header(header const& header, size_t height) {
    if (get_next_header_height(internal_db()) != height) {
        return error::store_block_invalid_height;
    }

    if (height > 0) {
        auto const prev_hash = get_previous_hash(internal_db(), height);
        if ( ! prev_hash || header.previous_block_hash() != *prev_hash) {
            return error::store_block_missing_parent;
        }
    }

    auto res = internal_db_->push_header(header, height);
    if ( ! succeed(res)) {
        return error::database_push_failed;
    }
    return error::success;
}

// Add a header with explicit ABLA state for headers-first sync.
// This is designed for write exclusivity and read concurrency.
code data_base::push_header(header const& header, size_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size) {
    if (get_next_header_height(internal_db()) != height) {
        return error::store_block_invalid_height;
    }

    if (height > 0) {
        auto const prev_hash = get_previous_hash(internal_db(), height);
        if ( ! prev_hash || header.previous_block_hash() != *prev_hash) {
            return error::store_block_missing_parent;
        }
    }

    auto res = internal_db_->push_header(header, height, block_size, control_block_size, elastic_buffer_size);
    if ( ! succeed(res)) {
        return error::database_push_failed;
    }
    return error::success;
}

// Push multiple headers in a single transaction (batch)
code data_base::push_headers_batch(header::list const& headers, size_t start_height) {
    if (headers.empty()) {
        return error::success;
    }

    auto res = internal_db_->push_headers_batch(headers, uint32_t(start_height));
    if ( ! succeed(res)) {
        return error::database_push_failed;
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

// Coroutine writers.
// ----------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)

// Push a single block to the database (synchronous helper).
code data_base::do_push(block_const_ptr block, size_t height, uint32_t median_time_past) {
    auto res = internal_db_->push_block(*block, height, median_time_past);
    if ( ! succeed(res)) {
        return error::database_concurrent_push_failed;
    }
    block->validation.end_push = asio::steady_clock::now();
    return error::success;
}

// Push all blocks sequentially (one after another).
::asio::awaitable<code> data_base::push_all_sequential(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height) {
    DEBUG_ONLY(*safe_add(in_blocks->size(), first_height));

    size_t height = first_height;
    for (auto const& block : *in_blocks) {
        auto const median_time_past = block->header().validation.median_time_past;

        // Set push start time for the block.
        block->validation.start_push = asio::steady_clock::now();

        // Push block synchronously.
        auto const ec = do_push(block, height, median_time_past);
        if (ec) {
            co_return ec;
        }
        ++height;
    }
    co_return error::success;
}

// Push all blocks in parallel (dispatch all, collect results).
::asio::awaitable<code> data_base::push_all_parallel(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height) {
    DEBUG_ONLY(*safe_add(in_blocks->size(), first_height));

    auto const count = in_blocks->size();
    if (count == 0) {
        co_return error::success;
    }

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor, count);

    // Dispatch all push operations in parallel.
    size_t height = first_height;
    for (auto const& block : *in_blocks) {
        auto const median_time_past = block->header().validation.median_time_past;
        auto const block_height = height;

        // Set push start time for the block.
        block->validation.start_push = asio::steady_clock::now();

        ::asio::post(executor, [this, block, block_height, median_time_past, channel]() {
            auto const ec = do_push(block, block_height, median_time_past);
            channel->try_send(std::error_code{}, ec);
        });

        ++height;
    }

    // Collect results from all parallel operations.
    code final_result = error::success;
    for (size_t i = 0; i < count; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            // Channel error (shouldn't happen).
            final_result = error::database_concurrent_push_failed;
        } else if (result && !final_result) {
            // Store first error encountered.
            final_result = result;
        }
    }

    co_return final_result;
}

// Push all blocks starting at first_height.
// Selects parallel or sequential based on settings_.parallel_block_push.
::asio::awaitable<code> data_base::push_all(executor_type executor, block_const_ptr_list_const_ptr in_blocks, size_t first_height) {
    if (settings_.parallel_block_push) {
        co_return co_await push_all_parallel(executor, in_blocks, first_height);
    } else {
        co_return co_await push_all_sequential(executor, in_blocks, first_height);
    }
}

// Pop the set of blocks above the given hash.
// Returns the popped blocks or an error if the database is corrupt or the hash doesn't exist.
awaitable_expected<block_const_ptr_list_ptr> data_base::pop_above(executor_type executor, hash_digest const& fork_hash) {
    auto out_blocks = std::make_shared<block_const_ptr_list>();

    auto const header_result = internal_db_->get_header(fork_hash);
    if ( ! header_result) {
        co_return std::unexpected(error::chain_reorganization_failed);
    }

    // The fork point does not exist or failed to get it or the top, fail.
    auto const heights = internal_db_->get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(error::chain_reorganization_failed);
    }
    auto const top = heights->block;

    auto const fork = header_result->second;
    auto const size = top - fork;

    // The fork is at the top of the chain, nothing to pop.
    if (size == 0) {
        co_return out_blocks;
    }

    // If the fork is at the top there is one block to pop, and so on.
    out_blocks->reserve(size);

    // Enqueue blocks so .front() is fork + 1 and .back() is top.
    for (size_t height = top; height > fork; --height) {
        domain::message::block next;

        if ( ! pop(next)) {
            co_return std::unexpected(error::database_insert_failed);
        }

        KTH_ASSERT(next.is_valid());
        auto block = std::make_shared<domain::message::block const>(std::move(next));
        out_blocks->insert(out_blocks->begin(), block);
    }

    co_return out_blocks;
}

code data_base::prune_reorg() {
    auto res = internal_db_->prune();
    if ( ! succeed_prune(res)) {
        spdlog::error("[database] Error pruning the reorganization pool, code: {}", static_cast<std::underlying_type<result_code>::type>(res));
        return error::unknown;
    }
    return error::success;
}

// Invoke pop_above and then push_all.
// Returns the outgoing blocks that were popped.
awaitable_expected<block_const_ptr_list_ptr> data_base::reorganize(
    executor_type executor,
    infrastructure::config::checkpoint const& fork_point,
    block_const_ptr_list_const_ptr incoming_blocks) {

    // Pop blocks above the fork point.
    auto pop_result = co_await pop_above(executor, fork_point.hash());
    if ( ! pop_result.has_value()) {
        co_return std::unexpected(pop_result.error());
    }

    auto outgoing_blocks = std::move(pop_result.value());

    // Push incoming blocks.
    auto const next_height = *safe_add(fork_point.height(), size_t(1));
    auto const push_ec = co_await push_all(executor, incoming_blocks, next_height);
    if (push_ec) {
        co_return std::unexpected(push_ec);
    }

    co_return outgoing_blocks;
}
#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database
