// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/database/flat_file_pos.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <latch>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>

#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/database.hpp>
#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/timer.hpp>

#include <asio/co_spawn.hpp>
#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

#include <utxoz/logging.hpp>

namespace kth {

time_t floor_subtract(time_t left, time_t right) {
    static auto const floor = (std::numeric_limits<time_t>::min)();
    return right >= left ? floor : left - right;
}

} // namespace kth

namespace kth::blockchain {

using spent_value_type = std::pair<hash_digest, uint32_t>;
using spent_container = std::unordered_set<spent_value_type>;

} // namespace kth::blockchain

namespace std {

template <>
struct hash<kth::blockchain::spent_value_type> {
    size_t operator()(kth::blockchain::spent_value_type const& point) const {
        size_t seed = 0;
        boost::hash_combine(seed, point.first);
        boost::hash_combine(seed, point.second);
        return seed;
    }
};

} // namespace std

namespace kth {

#if defined(KTH_WITH_MEMPOOL)
namespace mining {
mempool* mempool::candidate_index_t::parent_ = nullptr;
} // namespace mining
#endif

namespace blockchain {

using namespace kd::config;
using namespace kd::message;
using namespace kth::database;
using namespace std::placeholders;

#define NAME "block_chain"

static auto const hour_seconds = 3600u;

// =============================================================================
// CONSTRUCTION
// =============================================================================

block_chain::block_chain(blockchain::settings const& chain_settings,
                         database::settings const& database_settings,
                         domain::config::network network,
                         bool relay_transactions)
    : stopped_(true)
    , settings_(chain_settings)
    , notify_limit_seconds_(chain_settings.notify_limit_hours * hour_seconds)
    , chain_state_populator_(*this, chain_settings, network)
    , database_(database_settings)
    , validation_mutex_(relay_transactions)
    , priority_pool_("priority", std::min(size_t(8), thread_ceiling(chain_settings.cores)))
#if defined(KTH_WITH_MEMPOOL)
    , mempool_(chain_settings.mempool_max_template_size, chain_settings.mempool_size_multiplier)
    , transaction_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings, mempool_)
    , block_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings, network, relay_transactions, mempool_)
#else
    , transaction_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings)
    , block_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings, network, relay_transactions)
#endif
{
    spdlog::debug("[blockchain] block_chain constructor completed successfully");
}

block_chain::~block_chain() {
    (void)close();
}

// =============================================================================
// LIFECYCLE
// =============================================================================

bool block_chain::start(uint32_t disk_magic) {
    stopped_ = false;

    if ( ! database_.open()) {
        spdlog::error("[blockchain] Failed to open database.");
        return false;
    }

    // Open UTXO-Z database (in a subdirectory of the main database)
    utxoz::set_log_prefix("UTXO-Z");
    auto utxoz_path = database_.internal_db_dir.parent_path() / "utxoz";
    if ( ! utxoz_db_.open(utxoz_path)) {
        spdlog::error("[blockchain] Failed to open UTXO-Z database at {}", utxoz_path.string());
        return false;
    }
    spdlog::info("[blockchain] UTXO-Z database opened at {}", utxoz_path.string());

    // Initialize flat file block storage
    // Convert disk magic to little-endian bytes for file header
    auto blocks_path = database_.internal_db_dir.parent_path() / "blocks";
    auto magic = to_little_endian(disk_magic);
    block_store_ = std::make_unique<database::block_store>(blocks_path, magic);
    if ( ! block_store_->initialize()) {
        spdlog::error("[blockchain] Failed to initialize block store at {}", blocks_path.string());
        return false;
    }
    spdlog::info("[blockchain] Block store initialized at {}", blocks_path.string());

    pool_state_ = chain_state_populator_.populate();
    if ( ! pool_state_) {
        spdlog::error("[blockchain] Failed to initialize chain state.");
        return false;
    }

    if ( ! transaction_organizer_.start()) {
        spdlog::error("[blockchain] Failed to start transaction organizer.");
        return false;
    }

    if ( ! block_organizer_.start()) {
        spdlog::error("[blockchain] Failed to start block organizer.");
        return false;
    }

    // Load all headers from database into header_index
    // This allows resuming sync from where we left off
    auto const heights = get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] Failed to get last heights from database.");
        return false;
    }

    spdlog::info("[blockchain] Database state: header_height={}, block_height={}", heights->header, heights->block);

    if (heights->header == 0) {
        // Only genesis in DB - just add genesis to index
        auto const genesis = get_header(0);
        if (genesis) {
            auto const hash = genesis->hash();
            auto const [inserted, idx, capacity_warning] = header_index_.add(hash, *genesis);
            if ( ! inserted) {
                spdlog::error("[blockchain] Failed to initialize header index with genesis block.");
                return false;
            }
            spdlog::info("[blockchain] Header index initialized with genesis: {}", encode_hash(hash));
        }
    } else {
        // Load all headers from DB into header_index
        spdlog::info("[blockchain] Loading {} headers from database into header_index...", heights->header + 1);

        auto const load_start = std::chrono::steady_clock::now();

        // Load in batches to avoid memory spikes
        constexpr size_t batch_size = 10000;
        size_t loaded = 0;

        for (size_t from = 0; from <= heights->header; from += batch_size) {
            auto const to = std::min(from + batch_size - 1, size_t(heights->header));
            auto const headers_result = get_headers(from, to);

            if ( ! headers_result) {
                spdlog::error("[blockchain] Failed to load headers from {} to {}", from, to);
                return false;
            }

            size_t height = from;
            for (auto const& header : *headers_result) {
                auto const hash = header.hash();
                auto const [inserted, idx, capacity_warning] = header_index_.add(hash, header);
                if ( ! inserted && height > 0) {
                    // Genesis might already be added, ignore that case
                    spdlog::warn("[blockchain] Failed to add header at height {} to index", height);
                }
                ++height;
                ++loaded;
            }

            // Log progress every 100k headers
            if (loaded % 100000 < batch_size && loaded > 0) {
                spdlog::info("[blockchain] Loaded {}/{} headers into index...", loaded, heights->header + 1);
            }
        }

        auto const elapsed = std::chrono::steady_clock::now() - load_start;
        auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        spdlog::info("[blockchain] Loaded {} headers into header_index in {}ms", loaded, elapsed_ms);
    }

    // Restore block file positions in header_index from flat files
    if (block_store_ && heights->block > 0) {
        spdlog::info("[blockchain] Scanning flat files to restore block positions...");
        auto const scan_start = std::chrono::steady_clock::now();

        size_t restored = 0;
        auto const scanned = block_store_->scan_block_positions(
            [this, &restored](int32_t file_num, uint32_t data_pos, hash_digest const& hash) {
                auto const idx = header_index_.find(hash);
                if (idx != header_index::null_index) {
                    header_index_.set_block_pos(idx, static_cast<int16_t>(file_num), data_pos);
                    header_index_.add_status(idx, header_status::have_data);
                    ++restored;
                }
            }
        );

        auto const scan_elapsed = std::chrono::steady_clock::now() - scan_start;
        auto const scan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(scan_elapsed).count();
        spdlog::info("[blockchain] Scanned {} blocks, restored {} positions in {}ms",
            scanned, restored, scan_ms);
    }

    return true;
}

bool block_chain::stop() {
    stopped_ = true;

    validation_mutex_.lock_high_priority();
    auto result = transaction_organizer_.stop() && block_organizer_.stop();
    priority_pool_.stop();
    validation_mutex_.unlock_high_priority();

    return result;
}

bool block_chain::close() {
    auto const result = stop();
    priority_pool_.join();
    utxoz_db_.close();
    return result && database_.close();
}

bool block_chain::stopped() const {
    return stopped_;
}

// =============================================================================
// ORGANIZERS (Core blockchain operations)
// =============================================================================

::asio::awaitable<code> block_chain::organize(block_const_ptr block, bool headers_pre_validated) {
    co_return co_await block_organizer_.organize(block, headers_pre_validated);
}

::asio::awaitable<code> block_chain::organize_fast(std::shared_ptr<domain::chain::light_block const> block, size_t height) {
    // Fast IBD: merkle validation + (later) store block data to flat files.
    //
    // Threading model:
    // - Work is posted to priority_pool_ (appears parallel)
    // - But caller does co_await before proceeding (sequential from caller's perspective)
    // - Since there's only ONE caller (block_validation_task), operations are effectively sequential
    // - Therefore block_store_ and header_index_ don't need mutex protection for these operations
    //
    // Why post to pool instead of running inline?
    // - Avoids blocking the main executor while doing I/O
    // - Allows the coroutine to suspend without consuming a system thread
    //
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, block, height, channel]() {
        // Validate merkle root (ensures transactions match header)
        // This is the only validation needed since header was already validated
        if ( ! block->is_valid_merkle_root()) {
            spdlog::error("[blockchain] Merkle mismatch at height {}", height);
            channel->try_send(std::error_code{}, error::merkle_mismatch);
            return;
        }

#if 0  // Stage 3: disk storage (not yet enabled)
        // Save block to flat files (sequential I/O)
        auto pos = block_store_->save_block(*block, static_cast<uint32_t>(height));
        if (pos.is_null()) {
            spdlog::error("[blockchain] Failed to save block {} to flat files", height);
            channel->try_send(std::error_code{}, error::operation_failed);
            return;
        }

        // Update header_index with block file position
        auto const block_hash = block->header().hash();
        auto const idx = header_index_.find(block_hash);
        if (idx != header_index::null_index) {
            header_index_.set_block_pos(idx, static_cast<int16_t>(pos.file), pos.pos);
            header_index_.add_status(idx, header_status::have_data);
        }

        // TODO(fernando): Remove this once we fully migrate away from LMDB for blocks
        // Also update last block height property in LMDB (for compatibility)
        auto result = database_.internal_db().set_last_block_height(static_cast<uint32_t>(height));
        if (result != database::result_code::success) {
            spdlog::warn("[blockchain] Failed to update last_height in LMDB: {}", static_cast<int>(result));
        }
#endif

        channel->try_send(std::error_code{}, error::success);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

::asio::awaitable<code> block_chain::store_chunk(
    std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
    uint32_t start_height
) {
    auto const n = blocks.size();
    if (n == 0) co_return error::success;

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;

    // Phase 1: Allocate positions for all N blocks (single post, serial, fast)
    // allocate_block_space() must be called serially — guaranteed because
    // block_storage_task is a single coroutine doing co_await store_chunk() one at a time.
    auto positions = std::make_shared<std::vector<database::flat_file_pos>>(n);
    auto alloc_ch = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, &blocks, start_height, n, positions, alloc_ch]() {
        for (size_t i = 0; i < n; ++i) {
            auto const height = start_height + static_cast<uint32_t>(i);
            auto const& block = blocks[i];
            auto const raw_size = static_cast<uint32_t>(block->raw_data().size());

            auto pos = block_store_->allocate_block_space(raw_size, height, block->header().timestamp());
            if (pos.is_null()) {
                spdlog::error("[blockchain] Failed to allocate space for block {}", height);
                alloc_ch->try_send(std::error_code{}, error::operation_failed);
                return;
            }
            (*positions)[i] = pos;
        }
        alloc_ch->try_send(std::error_code{}, error::success);
    });

    {
        auto [ec, result] = co_await alloc_ch->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec || result) co_return result ? result : error::operation_failed;
    }

    // Phase 2: Write all N blocks in parallel (N posts, each opens its own FILE*)
    auto write_ch = std::make_shared<result_channel>(priority_pool_.get_executor(), n);

    for (size_t i = 0; i < n; ++i) {
        ::asio::post(priority_pool_.get_executor(), [this, &blocks, i, positions, write_ch]() {
            auto data_pos = block_store_->write_block_at(blocks[i]->raw_data(), (*positions)[i]);
            if (data_pos.is_null()) {
                write_ch->try_send(std::error_code{}, error::operation_failed);
                return;
            }
            // Store the data position (after header) for header_index
            (*positions)[i] = data_pos;
            write_ch->try_send(std::error_code{}, error::success);
        });
    }

    // Wait for all writes to complete
    code first_error;
    for (size_t i = 0; i < n; ++i) {
        auto [ec, result] = co_await write_ch->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec && !first_error) first_error = error::operation_failed;
        if (result && !first_error) first_error = result;
    }
    if (first_error) co_return first_error;

    // Phase 3: Update header_index with data positions (no I/O, fast)
    // Each block writes to its own index entry — safe without locks.
    for (size_t i = 0; i < n; ++i) {
        auto const& block = blocks[i];
        auto const block_hash = block->header().hash();
        auto const idx = header_index_.find(block_hash);
        if (idx != header_index::null_index) {
            auto const& data_pos = (*positions)[i];
            header_index_.set_block_pos(idx, static_cast<int16_t>(data_pos.file), data_pos.pos);
            header_index_.add_status(idx, header_status::have_data);
        }
    }

    co_return error::success;
}

// Toggle between validation strategies:
//   true  = 1 task per chunk (serial merkle on single pool thread)
//   false = 1 task per block (parallel merkle across pool threads)
static constexpr bool chunk_serial_validation = true;

::asio::awaitable<code> block_chain::validate_chunk(
    std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
    uint32_t start_height
) {
    auto const n = blocks.size();
    if (n == 0) co_return error::success;

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    auto const chunk_start_time = std::chrono::steady_clock::now();

    if constexpr (chunk_serial_validation) {
        // =====================================================================
        // Variant A: 1 task per chunk — serial merkle on a single pool thread
        // =====================================================================
        ::asio::post(priority_pool_.get_executor(), [&blocks, start_height, n, channel]() {
            auto const tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
            auto const t0 = std::chrono::steady_clock::now();

            code first_error;
            for (size_t i = 0; i < n; ++i) {
                if ( ! blocks[i]->is_valid_merkle_root()) {
                    spdlog::error("[validate_chunk:serial] Merkle MISMATCH height {} thread {}",
                        start_height + i, tid);
                    if ( ! first_error) {
                        first_error = error::merkle_mismatch;
                    }
                }
            }

            auto const elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
            spdlog::debug("[validate_chunk:serial] {} blocks at {} on thread {} in {}us ({:.1f}us/blk)",
                n, start_height, tid, elapsed_us, static_cast<double>(elapsed_us) / n);

            channel->try_send(std::error_code{}, first_error);
        });

        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            co_return error::operation_failed;
        }

        auto const chunk_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - chunk_start_time).count();
        spdlog::debug("[validate_chunk:serial] chunk at {} wall time {}us", start_height, chunk_elapsed_us);

        co_return result;
    } else {
        // =====================================================================
        // Variant B: 1 task per block — parallel merkle across pool threads
        // =====================================================================
        auto parallel_channel = std::make_shared<result_channel>(priority_pool_.get_executor(), n);

        for (size_t i = 0; i < n; ++i) {
            ::asio::post(priority_pool_.get_executor(), [block = blocks[i], height = start_height + i, parallel_channel]() {
                auto const t0 = std::chrono::steady_clock::now();
                auto const tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

                bool valid = block->is_valid_merkle_root();

                auto const elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0).count();

                if ( ! valid) {
                    spdlog::error("[validate_chunk:parallel] Merkle MISMATCH height {} thread {} ({}us)", height, tid, elapsed_us);
                    parallel_channel->try_send(std::error_code{}, error::merkle_mismatch);
                    return;
                }
                spdlog::trace("[validate_chunk:parallel] height {} thread {} ({}us)", height, tid, elapsed_us);
                parallel_channel->try_send(std::error_code{}, error::success);
            });
        }

        code final_result;
        for (size_t i = 0; i < n; ++i) {
            auto [ec, result] = co_await parallel_channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return error::operation_failed;
            }
            if (result && !final_result) {
                final_result = result;
            }
        }

        auto const chunk_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - chunk_start_time).count();
        spdlog::debug("[validate_chunk:parallel] chunk at {} ({} blocks) wall time {}us ({:.1f}us/blk)",
            start_height, n, chunk_elapsed_us, static_cast<double>(chunk_elapsed_us) / n);

        co_return final_result;
    }
}

::asio::awaitable<code> block_chain::store_block(
    std::shared_ptr<domain::chain::light_block const> const& block,
    uint32_t height
) {
    // Same post+await pattern as validate_chunk() and organize_fast():
    // Post disk I/O to priority_pool_ so the network executor stays free.
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, block, height, channel]() {
        // 1. Save block to flat files (sequential I/O)
        auto pos = block_store_->save_block_raw(block->raw_data(), height, block->header().timestamp());
        if (pos.is_null()) {
            spdlog::error("[blockchain] Failed to save block {} to flat files", height);
            channel->try_send(std::error_code{}, error::operation_failed);
            return;
        }

        // 2. Update header_index with block file position
        auto const block_hash = block->header().hash();
        auto const idx = header_index_.find(block_hash);
        if (idx != header_index::null_index) {
            header_index_.set_block_pos(idx, static_cast<int16_t>(pos.file), pos.pos);
            header_index_.add_status(idx, header_status::have_data);
        }

        // 3. Update last block height in LMDB (for compatibility / UTXO build)
        auto result = database_.internal_db().set_last_block_height(height);
        if (result != database::result_code::success) {
            spdlog::warn("[blockchain] Failed to update last_height in LMDB for height {}: {}",
                height, static_cast<int>(result));
        }

        channel->try_send(std::error_code{}, error::success);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

::asio::awaitable<code> block_chain::organize(transaction_const_ptr tx) {
    co_return co_await transaction_organizer_.organize(tx);
}

::asio::awaitable<code> block_chain::organize(double_spend_proof_const_ptr ds_proof) {
    co_return co_await transaction_organizer_.organize(ds_proof);
}

::asio::awaitable<code> block_chain::organize_header(header_const_ptr header) {
    if (stopped()) {
        co_return error::service_stopped;
    }

    // Get current header height
    auto const heights = get_last_heights();
    if ( ! heights) {
        co_return error::operation_failed;
    }

    auto const next_height = heights->header + 1;

    // Store header in database with ABLA state = 0
    // The correct ABLA state will be set when the full block arrives via push_block
    auto const ec = database_.push_header(*header, next_height);
    if (ec) {
        co_return ec;
    }

    co_return error::success;
}

code block_chain::organize_headers_batch(domain::chain::header::list const& headers, size_t start_height) {
    if (stopped()) {
        return error::service_stopped;
    }

    if (headers.empty()) {
        return error::success;
    }

    return database_.push_headers_batch(headers, start_height);
}

#if ! defined(KTH_DB_READONLY)

awaitable_expected<block_const_ptr_list_ptr> block_chain::reorganize(
    infrastructure::config::checkpoint const& fork_point,
    block_const_ptr_list_const_ptr incoming_blocks) {

    // ==========================================================================
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    // ==========================================================================
    (void)incoming_blocks;
    (void)fork_point;
    co_return std::unexpected(error::operation_failed);
}

::asio::awaitable<code> block_chain::push(transaction_const_ptr tx) {
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, tx, channel]() {
        auto const result = push_sync(tx);
        channel->try_send(std::error_code{}, result);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

code block_chain::push_sync(transaction_const_ptr tx) {
    return database_.push(*tx, chain_state()->enabled_forks());
}

bool block_chain::insert(block_const_ptr block, size_t height) {
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    (void)block;
    (void)height;
    return false;
}

void block_chain::prune_reorg_async() {
    if ( ! is_stale()) {
        ::asio::post(priority_pool_.get_executor(), [this]() {
            database_.prune_reorg();
        });
    }
}

std::expected<uint32_t, database::result_code> block_chain::get_utxo_built_height() const {
    return database_.internal_db().get_utxo_built_height();
}

database::result_code block_chain::set_utxo_built_height(uint32_t height) {
    return database_.internal_db().set_utxo_built_height(height);
}

database::result_code block_chain::set_last_block_height(uint32_t height) {
    return database_.internal_db().set_last_block_height(height);
}

// DEPRECATED: UTXO storage moved to UTXOZ
database::result_code block_chain::clear_utxo_set() {
    return database::result_code::success;
}

size_t block_chain::utxo_deferred_deletions_size() const {
    return utxoz_db_.deferred_deletions_size();
}

std::pair<size_t, std::vector<utxoz::deferred_deletion_entry>> block_chain::utxo_process_pending_deletions() {
    return utxoz_db_.process_pending_deletions();
}

void block_chain::utxo_compact() {
    utxoz_db_.compact();
}

#endif // ! defined(KTH_DB_READONLY)

// =============================================================================
// CHAIN STATE
// =============================================================================

domain::chain::chain_state::ptr block_chain::chain_state() const {
    shared_lock lock(pool_state_mutex_);
    return pool_state_;
}

domain::chain::chain_state::ptr block_chain::chain_state(branch::const_ptr branch) const {
    return chain_state_populator_.populate(chain_state(), branch);
}

code block_chain::set_chain_state(domain::chain::chain_state::ptr previous) {
    unique_lock lock(pool_state_mutex_);
    pool_state_ = chain_state_populator_.populate(previous);
    return pool_state_ ? error::success : error::pool_state_failed;
}

// =============================================================================
// SUBSCRIPTIONS
// =============================================================================

block_chain::block_channel_ptr block_chain::subscribe_blockchain() {
    return block_organizer_.subscribe();
}

block_chain::transaction_channel_ptr block_chain::subscribe_transaction() {
    return transaction_organizer_.subscribe();
}

block_chain::ds_proof_channel_ptr block_chain::subscribe_ds_proof() {
    return transaction_organizer_.subscribe_ds_proof();
}

void block_chain::unsubscribe_blockchain(block_channel_ptr const& channel) {
    block_organizer_.unsubscribe(channel);
}

void block_chain::unsubscribe_transaction(transaction_channel_ptr const& channel) {
    transaction_organizer_.unsubscribe(channel);
}

void block_chain::unsubscribe_ds_proof(ds_proof_channel_ptr const& channel) {
    transaction_organizer_.unsubscribe_ds_proof(channel);
}

// =============================================================================
// VALIDATION
// =============================================================================

::asio::awaitable<code> block_chain::transaction_validate(transaction_const_ptr tx) const {
    co_return co_await transaction_organizer_.transaction_validate(tx);
}

// =============================================================================
// PROPERTIES
// =============================================================================

bool block_chain::is_stale() const {
    if (notify_limit_seconds_ == 0) {
        return false;
    }

    auto const top = last_block_.load();

    uint32_t last_timestamp = 0;
    if ( ! top) {
        auto const heights = get_last_heights();
        if (heights) {
            auto const last_height = heights->block;
            auto const last_header = get_header(last_height);
            if (last_header) {
                last_timestamp = last_header->timestamp();
            }
        }
    }

    auto const timestamp = top ? top->header().timestamp() : last_timestamp;
    return timestamp < floor_subtract(zulu_time(), notify_limit_seconds_);
}

settings const& block_chain::chain_settings() const {
    return settings_;
}

block_chain::executor_type block_chain::executor() const {
    return priority_pool_.get_executor();
}

#if defined(KTH_WITH_MEMPOOL)
std::pair<std::vector<kth::mining::transaction_element>, uint64_t> block_chain::get_block_template() const {
    return mempool_.get_block_template();
}
#endif

// =============================================================================
// DATABASE READERS (Low-level, NOT thread safe)
// =============================================================================

// bool block_chain::get_last_height(size_t& out_height) const {
//     auto result = database_.internal_db().get_last_heights();
//     if ( ! result) {
//         return false;
//     }
//     out_height = result->first;  // header_height
//     return true;
// }

std::expected<heights_t, database::result_code> block_chain::get_last_heights() const {
    return database_.internal_db().get_last_heights();
}

std::expected<domain::chain::header, database::result_code> block_chain::get_header(size_t height) const {
    return database_.internal_db().get_header(height);
}

std::expected<database::header_with_abla_state_t, database::result_code> block_chain::get_header_and_abla_state(size_t height) const {
    return database_.internal_db().get_header_and_abla_state(height);
}

std::expected<domain::chain::header::list, database::result_code> block_chain::get_headers(size_t from, size_t to) const {
    return database_.internal_db().get_headers(from, to);
}

std::expected<size_t, database::result_code> block_chain::get_height(hash_digest const& block_hash) const {
    auto result = database_.internal_db().get_header(block_hash);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->second;
}

std::expected<uint32_t, database::result_code> block_chain::get_bits(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->bits();
}

std::expected<uint32_t, database::result_code> block_chain::get_timestamp(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->timestamp();
}

std::expected<uint32_t, database::result_code> block_chain::get_version(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->version();
}

std::expected<hash_digest, database::result_code> block_chain::get_block_hash(size_t height) const {
    auto const result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->hash();
}

bool block_chain::header_exists(hash_digest const& block_hash) const {
    return database_.internal_db().get_header(block_hash).has_value();
}

bool block_chain::block_exists(hash_digest const& block_hash) const {
    // Check if full block exists (not just header)
    // With headers-first sync, headers may exist without full blocks
    auto const header_result = database_.internal_db().get_header(block_hash);
    if (!header_result) {
        return false;  // Header doesn't exist, so block doesn't exist
    }

    // Header exists - check if we have the full block
    auto const heights = database_.internal_db().get_last_heights();
    if (!heights) {
        return false;
    }

    auto const this_block_height = header_result->second;

    // Block exists only if its height <= block_height
    return this_block_height <= heights->block;
}

std::expected<uint256_t, database::result_code> block_chain::get_branch_work(uint256_t const& maximum, size_t from_height) const {
    auto const heights = get_last_heights();
    if ( ! heights) {
        return std::unexpected(heights.error());
    }
    // Use block_height (not header_height) for work comparison
    // With headers-first sync, we may have headers without full blocks
    auto const top = heights->block;

    uint256_t out_work = 0;
    for (uint32_t height = from_height; height <= top && out_work < maximum; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            return std::unexpected(result.error());
        }
        out_work += domain::chain::header::proof(result->bits());
    }

    return out_work;
}

std::expected<block_chain::output_info, database::result_code> block_chain::get_output(
    domain::chain::output_point const& outpoint,
    size_t branch_height, bool /*require_confirmed*/) const {

    auto const tx = database_.internal_db().get_transaction(outpoint.hash(), branch_height);
    if ( ! tx) {
        return std::unexpected(tx.error());
    }

    return output_info{
        tx->transaction().outputs()[outpoint.index()],
        tx->height(),
        tx->median_time_past(),
        tx->position() == 0
    };
}

std::expected<block_chain::output_info, database::result_code> block_chain::get_utxo(
    domain::chain::output_point const& outpoint, size_t branch_height) const {

    // Use UTXO-Z high-performance database
    auto entry = utxoz_db_.find(outpoint, static_cast<uint32_t>(branch_height));
    if ( ! entry) {
        return std::unexpected(entry.error());
    }
    if (entry->height() > branch_height) {
        return std::unexpected(database::result_code::key_not_found);
    }

    return output_info{
        entry->output(),
        entry->height(),
        entry->median_time_past(),
        entry->coinbase()
    };
}

std::expected<database::internal_database::utxo_pool_t, database::result_code> block_chain::get_utxo_pool_from(uint32_t from, uint32_t to) const {
    return database_.internal_db().get_utxo_pool_from(from, to);
}

std::expected<std::pair<size_t, size_t>, database::result_code> block_chain::get_transaction_position(
    hash_digest const& hash, bool require_confirmed) const {

    auto const result = database_.internal_db().get_transaction(hash, max_size_t);

    if (result) {
        return std::pair{result->height(), result->position()};
    }

    if (require_confirmed) {
        return std::unexpected(result.error());
    }

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( ! result2) {
        return std::unexpected(result2.error());
    }

    return std::pair{result2->height(), position_max};
}

// =============================================================================
// FETCH OPERATIONS (Thread safe, coroutine-based)
// =============================================================================

// DEPRECATED: Block storage moved to flat files (blk*.dat)
// These functions will return not_found until we implement reading from flat files
awaitable_expected<std::pair<block_const_ptr, size_t>>
block_chain::fetch_block(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const cached = last_block_.load();
    if (cached && cached->validation.state && cached->validation.state->height() == height) {
        co_return std::pair{cached, height};
    }

    // LMDB block storage removed - blocks now in flat files
    (void)height;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<block_const_ptr, size_t>>
block_chain::fetch_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const cached = last_block_.load();
    if (cached && cached->validation.state && cached->hash() == hash) {
        co_return std::pair{cached, cached->validation.state->height()};
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

std::expected<domain::chain::block::list, database::result_code>
block_chain::fetch_blocks(uint32_t from, uint32_t to) const {
    // LMDB block storage removed - blocks now in flat files
    (void)from;
    (void)to;
    return std::unexpected(database::result_code::other);
}

std::expected<std::vector<data_chunk>, database::result_code>
block_chain::fetch_blocks_raw(uint32_t from, uint32_t to) const {
    // Read blocks from flat files using positions stored in header_index
    // NOTE: During IBD, blocks are stored sequentially, so index == height
    std::vector<database::flat_file_pos> positions;
    positions.reserve(to - from + 1);

    for (uint32_t h = from; h <= to; ++h) {
        // In IBD mode, the header index matches the height
        auto const idx = static_cast<header_index::index_t>(h);

        if (idx >= header_index_.size()) {
            spdlog::error("[blockchain] fetch_blocks_raw: Height {} exceeds header_index size {}", h, header_index_.size());
            return std::unexpected(database::result_code::key_not_found);
        }

        if (!header_index_.has_block_data(idx)) {
            spdlog::error("[blockchain] fetch_blocks_raw: No block data for height {}", h);
            return std::unexpected(database::result_code::key_not_found);
        }

        auto file_num = header_index_.get_file_number(idx);
        auto data_pos = header_index_.get_data_pos(idx);
        positions.emplace_back(file_num, data_pos);
    }

    // Read all blocks from flat files
    auto result = block_store_->read_blocks_raw(positions);
    if (!result) {
        return std::unexpected(database::result_code::other);
    }

    return std::move(*result);
}

awaitable_expected<std::pair<header_ptr, size_t>>
block_chain::fetch_block_header(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::pair{std::make_shared<header>(std::move(*result)), height};
}

awaitable_expected<std::pair<header_ptr, size_t>>
block_chain::fetch_block_header(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_header(hash);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::pair{std::make_shared<header>(std::move(result->first)), result->second};
}

awaitable_expected<size_t>
block_chain::fetch_block_height(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_header(hash);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return result->second;
}

awaitable_expected<std::tuple<hash_digest, uint32_t, size_t>>
block_chain::fetch_block_hash_timestamp(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_header(height);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::tuple{result->hash(), result->timestamp(), height};
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::tuple<header_const_ptr, size_t, std::shared_ptr<hash_list>, uint64_t>>
block_chain::fetch_block_header_txs_size(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<heights_t>
block_chain::fetch_last_height() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_last_heights();
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return *result;
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::pair<merkle_block_ptr, size_t>>
block_chain::fetch_merkle_block(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)height;
    co_return std::unexpected(error::not_found);
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::pair<merkle_block_ptr, size_t>>
block_chain::fetch_merkle_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<compact_block_ptr, size_t>>
block_chain::fetch_compact_block(size_t /*height*/) const {
    co_return std::unexpected(error::not_implemented);
}

awaitable_expected<std::pair<compact_block_ptr, size_t>>
block_chain::fetch_compact_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto block_result = co_await fetch_block(hash);
    if ( ! block_result.has_value()) {
        co_return std::unexpected(block_result.error());
    }

    auto const& [blk, height] = block_result.value();
    auto compact = std::make_shared<compact_block>(compact_block::factory_from_block(*blk));

    co_return std::pair{compact, height};
}

awaitable_expected<std::tuple<transaction_const_ptr, size_t, size_t>>
block_chain::fetch_transaction(hash_digest const& hash, bool require_confirmed) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_transaction(hash, max_size_t);
    if (result) {
        co_return std::tuple{
            std::make_shared<const transaction>(result->transaction()),
            result->position(),
            result->height()
        };
    }

    if (require_confirmed) {
        co_return std::unexpected(error::not_found);
    }

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( ! result2) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::tuple{
        std::make_shared<const transaction>(result2->transaction()),
        position_max,
        result2->height()
    };
}

awaitable_expected<std::pair<size_t, size_t>>
block_chain::fetch_transaction_position(hash_digest const& hash, bool require_confirmed) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_transaction(hash, max_size_t);
    if (result) {
        co_return std::pair{result->position(), result->height()};
    }

    if (require_confirmed) {
        co_return std::unexpected(error::not_found);
    }

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( ! result2) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::pair{position_max, result2->height()};
}

awaitable_expected<transaction_const_ptr>
block_chain::fetch_unconfirmed_transaction(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::make_shared<const transaction>(result->transaction());
}

// Modified to use get_header instead of get_block (blocks now in flat files)
awaitable_expected<inventory_ptr>
block_chain::fetch_locator_block_hashes(get_blocks_const_ptr locator,
                                        hash_digest const& threshold,
                                        size_t limit) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    uint32_t start = 0;
    for (auto const& hash : locator->start_hashes()) {
        auto const result = database_.internal_db().get_header(hash);
        if (result) {
            start = result->second;
            break;
        }
    }

    auto begin = *safe_add(start, uint32_t(1));
    auto end = *safe_add(begin, uint32_t(limit));

    if (locator->stop_hash() != null_hash) {
        auto const result = database_.internal_db().get_header(locator->stop_hash());
        if (result) {
            end = std::min(result->second, end);
        }
    }

    if (threshold != null_hash) {
        auto const result = database_.internal_db().get_header(threshold);
        if (result) {
            begin = std::max(result->second, begin);
        }
    }

    auto hashes = std::make_shared<inventory>();
    hashes->inventories().reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            hashes->inventories().shrink_to_fit();
            break;
        }
        static auto const id = inventory::type_id::block;
        hashes->inventories().emplace_back(id, result->hash());
    }

    co_return hashes;
}

awaitable_expected<headers_ptr>
block_chain::fetch_locator_block_headers(get_headers_const_ptr locator,
                                         hash_digest const& threshold,
                                         size_t limit) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    size_t start = 0;
    for (auto const& hash : locator->start_hashes()) {
        auto const result = database_.internal_db().get_header(hash);
        if (result) {
            start = result->second;
            break;
        }
    }

    auto begin = *safe_add(start, size_t(1));
    auto end = *safe_add(begin, limit);

    if (locator->stop_hash() != null_hash) {
        auto const result = database_.internal_db().get_header(locator->stop_hash());
        if (result) {
            end = std::min(size_t(result->second), end);
        }
    }

    if (threshold != null_hash) {
        auto const result = database_.internal_db().get_header(threshold);
        if (result) {
            begin = std::max(size_t(result->second), begin);
        }
    }

    auto message = std::make_shared<domain::message::headers>();
    message->elements().reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            message->elements().shrink_to_fit();
            break;
        }
        message->elements().push_back(*result);
    }

    co_return message;
}

awaitable_expected<get_headers_ptr>
block_chain::fetch_block_locator(block::indexes const& heights) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto message = std::make_shared<domain::message::get_headers>();
    auto& hashes = message->start_hashes();
    hashes.reserve(heights.size());

    for (auto const height : heights) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            co_return std::unexpected(error::not_found);
        }
        hashes.push_back(result->hash());
    }

    co_return message;
}

awaitable_expected<domain::chain::input_point>
block_chain::fetch_spend(domain::chain::output_point const& outpoint) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

#if defined(KTH_DB_SPENDS)
    auto point = database_.spends().get(outpoint);
#else
    auto point = database_.internal_db().get_spend(outpoint);
#endif

    if ( ! point) {
        co_return std::unexpected(error::not_found);
    }

    co_return *point;
}

awaitable_expected<domain::chain::history_compact::list>
block_chain::fetch_history(short_hash const& address_hash, size_t limit, size_t from_height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

#if defined(KTH_DB_HISTORY)
    auto const result = database_.history().get(address_hash, limit, from_height);
#else
    auto const result = database_.internal_db().get_history(address_hash, limit, from_height);
#endif
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }
    co_return *result;
}

awaitable_expected<std::vector<hash_digest>>
block_chain::fetch_confirmed_transactions(short_hash const& address_hash,
                                          size_t limit, size_t from_height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

#if defined(KTH_DB_HISTORY)
    auto const result = database_.history().get_txns(address_hash, limit, from_height);
#else
    auto const result = database_.internal_db().get_history_txns(address_hash, limit, from_height);
#endif
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }
    co_return *result;
}

awaitable_expected<double_spend_proof_const_ptr>
block_chain::fetch_ds_proof(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }
    co_return co_await transaction_organizer_.fetch_ds_proof(hash);
}

// =============================================================================
// MEMPOOL / TRANSACTION POOL
// =============================================================================

awaitable_expected<std::pair<merkle_block_ptr, size_t>>
block_chain::fetch_template() const {
    co_return co_await transaction_organizer_.fetch_template();
}

awaitable_expected<inventory_ptr>
block_chain::fetch_mempool(size_t count_limit, uint64_t /*minimum_fee*/) const {
    co_return co_await transaction_organizer_.fetch_mempool(count_limit);
}

namespace {

std::tuple<uint8_t, uint8_t> get_address_versions(bool use_testnet_rules) {
    if (use_testnet_rules) {
        return {
            kth::domain::wallet::payment_address::testnet_p2kh,
            kth::domain::wallet::payment_address::testnet_p2sh};
    }
    return {
        kth::domain::wallet::payment_address::mainnet_p2kh,
        kth::domain::wallet::payment_address::mainnet_p2sh};
}

} // anonymous namespace

std::vector<mempool_transaction_summary> block_chain::get_mempool_transactions(
    std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const {

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<mempool_transaction_summary> ret;

    std::unordered_set<kth::domain::wallet::payment_address> addrs;
    for (auto const& payment_address : payment_addresses) {
        kth::domain::wallet::payment_address address(payment_address);
        if (address) {
            addrs.insert(address);
        }
    }

    auto const result = database_.internal_db().get_all_transaction_unconfirmed();
    if ( ! result) {
        return ret;
    }

    for (auto const& tx_res : *result) {
        auto const& tx = tx_res.transaction();
        size_t i = 0;

        for (auto const& output : tx.outputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                output.script(), encoding_p2kh, encoding_p2sh);
            for (auto const tx_address : tx_addresses) {
                if (tx_address && addrs.find(tx_address) != addrs.end()) {
                    ret.push_back(mempool_transaction_summary(
                        tx_address.encoded_cashaddr(false), kth::encode_hash(tx.hash()), "",
                        "", std::to_string(output.value()), i, tx_res.arrival_time()));
                }
            }
            ++i;
        }

        i = 0;
        for (auto const& input : tx.inputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                input.script(), encoding_p2kh, encoding_p2sh);
            for (auto const tx_address : tx_addresses) {
                if (tx_address && addrs.find(tx_address) != addrs.end()) {
                    auto const prev_tx = database_.internal_db().get_transaction(
                        input.previous_output().hash(), max_size_t);
                    if (prev_tx) {
                        ret.push_back(mempool_transaction_summary(
                            tx_address.encoded_cashaddr(false),
                            kth::encode_hash(tx.hash()),
                            kth::encode_hash(input.previous_output().hash()),
                            std::to_string(input.previous_output().index()),
                            "-" + std::to_string(prev_tx->transaction().outputs()[input.previous_output().index()].value()),
                            i, tx_res.arrival_time()));
                    }
                }
            }
            ++i;
        }
    }

    return ret;
}

std::vector<mempool_transaction_summary> block_chain::get_mempool_transactions(
    std::string const& payment_address, bool use_testnet_rules) const {
    return get_mempool_transactions(std::vector<std::string>{payment_address}, use_testnet_rules);
}

std::vector<domain::chain::transaction> block_chain::get_mempool_transactions_from_wallets(
    std::vector<domain::wallet::payment_address> const& payment_addresses,
    bool use_testnet_rules) const {

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<domain::chain::transaction> ret;
    auto const result = database_.internal_db().get_all_transaction_unconfirmed();
    if ( ! result) {
        return ret;
    }

    for (auto const& tx_res : *result) {
        auto const& tx = tx_res.transaction();
        bool inserted = false;

        for (auto iter_output = tx.outputs().begin();
             iter_output != tx.outputs().end() && !inserted; ++iter_output) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_output->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }

        for (auto iter_input = tx.inputs().begin();
             iter_input != tx.inputs().end() && !inserted; ++iter_input) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_input->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }
    }

    return ret;
}

block_chain::mempool_mini_hash_map block_chain::get_mempool_mini_hash_map(
    domain::message::compact_block const& block) const {

    if (stopped()) {
        return mempool_mini_hash_map();
    }

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash);
    auto k1 = from_little_endian_unsafe<uint64_t>(std::span{header_hash}.subspan(sizeof(uint64_t)));

    mempool_mini_hash_map mempool;
    auto const result = database_.internal_db().get_all_transaction_unconfirmed();
    if ( ! result) {
        return mempool;
    }

    for (auto const& tx_res : *result) {
        auto const& tx = tx_res.transaction();
        auto sh = sip_hash_uint256(k0, k1, tx.hash());
        mini_hash short_id;
        mempool.emplace(short_id, tx);
    }

    return mempool;
}

void block_chain::fill_tx_list_from_mempool(domain::message::compact_block const& block,
                                            size_t& mempool_count,
                                            std::vector<domain::chain::transaction>& txn_available,
                                            std::unordered_map<uint64_t, uint16_t> const& shorttxids) const {

    std::vector<bool> have_txn(txn_available.size());

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash);
    auto k1 = from_little_endian_unsafe<uint64_t>(std::span{header_hash}.subspan(sizeof(uint64_t)));

    auto const result = database_.internal_db().get_all_transaction_unconfirmed();
    if ( ! result) {
        return;
    }

    for (auto const& tx_res : *result) {
        auto const& tx = tx_res.transaction();

        uint64_t shortid = sip_hash_uint256(k0, k1, tx.hash()) & uint64_t(0xffffffffffff);

        auto idit = shorttxids.find(shortid);
        if (idit != shorttxids.end()) {
            if ( ! have_txn[idit->second]) {
                txn_available[idit->second] = tx;
                have_txn[idit->second] = true;
                ++mempool_count;
            } else {
                if (txn_available[idit->second].is_valid()) {
                    txn_available[idit->second] = domain::chain::transaction{};
                    --mempool_count;
                }
            }
        }
    }
}

// =============================================================================
// FILTERS
// =============================================================================

::asio::awaitable<code> block_chain::filter_blocks(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    block_organizer_.filter(message);
    auto& inventories = message->inventories();
    auto const& internal_db = database_.internal_db();

    for (auto it = inventories.begin(); it != inventories.end();) {
        auto const header = internal_db.get_header(it->hash());
        if (it->is_block_type() && header && header->first.is_valid()) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }

    co_return error::success;
}

::asio::awaitable<code> block_chain::filter_transactions(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    auto& inventories = message->inventories();

#if defined(KTH_WITH_MEMPOOL)
    auto validated_txs = mempool_.get_validated_txs_low();

    if (validated_txs.empty()) {
        co_return error::success;
    }

    for (auto it = inventories.begin(); it != inventories.end();) {
        auto found = validated_txs.find(it->hash());
        if (it->is_transaction_type() && found != validated_txs.end()) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }
#else
    for (auto it = inventories.begin(); it != inventories.end();) {
        auto const pos = get_transaction_position(it->hash(), false);
        if (it->is_transaction_type() && pos) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }
#endif

    co_return error::success;
}

}} // namespace kth::blockchain
