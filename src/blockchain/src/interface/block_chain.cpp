// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/database/flat_file_pos.hpp>

#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <latch>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>

#include <kth/blockchain/pools/mempool_config.hpp>
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


namespace blockchain {

using namespace kd::config;
using namespace kd::message;
using namespace kth::database;
using namespace std::placeholders;

namespace {

// TODO(mempool): KTH has no working mempool. The abandoned mining::mempool and
// the LMDB "transaction unconfirmed" storage that used to back the mempool
// queries and the tx sink have been removed to rebuild the mempool from
// scratch (concurrent hashmap + on-disk persistence). Until it lands, every
// mempool read/query and the tx sink abort loudly instead of silently
// returning wrong (empty) results. Tracked in issue #491.
[[noreturn]]
void mempool_not_implemented(char const* fn) {
    spdlog::critical(
        "[blockchain] {} was called but the mempool is not implemented "
        "(the LMDB unconfirmed-tx path was removed pending the new mempool; "
        "see TODO / issue #491). Aborting.", fn);
    std::abort();
}

} // anonymous namespace

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
    , transaction_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings)
    , block_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings, network, relay_transactions)
{
    spdlog::debug("[blockchain] block_chain constructor completed successfully");
    spdlog::info("[blockchain] Mempool backend: {}", mempool_backend_name);
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

#ifdef KTH_UTXOZ_COMPACT_MODE
    // Compact mode find resolution requires block_store and header_index.
    // block_store_ is not yet initialized here — we wire it below after initialization.
#endif

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
            auto const hash = domain::chain::hash(*genesis);
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
                auto const hash = domain::chain::hash(header);
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

#ifdef KTH_UTXOZ_COMPACT_MODE
    // Wire block_store and header_index to UTXO-Z for compact mode find resolution
    utxoz_db_.set_block_store(block_store_.get());
    utxoz_db_.set_header_index(&header_index_);
    spdlog::info("[blockchain] UTXO-Z compact mode: block_store and header_index wired for find resolution");
#endif

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
    // Persist the mempool now that admission is quiesced and before the DB dir
    // is torn down (the path is a plain sibling of the DB, still valid here).
    dump_mempool_to_disk();
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
    // Single-block acceptance was implemented on the pre-v1 LMDB storage
    // (block_organizer -> populate_block over the LMDB utxo_pool, reorg pool).
    // That storage is dead in the v1 node (flat files + UTXO-Z; IBD validates via
    // validate_block_batch + utxo_build), so this path can no longer accept a
    // block and is stubbed while the dead LMDB code is removed. Reimplementing it
    // on the v1 model is tracked in issue #563.
    (void)block;
    (void)headers_pre_validated;
    co_return error::not_implemented;
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

        // Stage 3 (flat-file block storage on top of header_index) is not
        // wired in yet; blocks live in LMDB via the database layer. Code
        // path for save_block + header_index.set_block_pos belongs in a
        // separate feature PR once flat_file_seq is ready.

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
        auto const block_hash = domain::chain::hash(block->header());
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
        auto const block_hash = domain::chain::hash(block->header());
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

code block_chain::replace_headers_from(domain::chain::header::list const& headers, size_t start_height) {
    if (stopped()) {
        return error::service_stopped;
    }

    return database_.replace_headers_from(headers, start_height);
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

code block_chain::push_sync(transaction_const_ptr /*tx*/) {
    // TODO(mempool): the tx sink used to write to the LMDB unconfirmed-tx
    // store, which has been removed. Reimplement against the new mempool.
    // See issue #491.
    mempool_not_implemented("block_chain::push_sync");
}

bool block_chain::insert(block_const_ptr block, size_t height) {
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    (void)block;
    (void)height;
    return false;
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

size_t block_chain::utxo_deferred_deletions_size() const {
    return utxoz_db_.deferred_deletions_size();
}

std::pair<size_t, std::vector<utxoz::deferred_deletion_entry>> block_chain::utxo_process_pending_deletions() {
    return utxoz_db_.process_pending_deletions();
}

size_t block_chain::utxo_deferred_lookups_size() const {
    return utxoz_db_.deferred_lookups_size();
}

std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, database::utxo_entry>, std::vector<utxoz::raw_outpoint>>
block_chain::utxo_process_pending_lookups() {
    return utxoz_db_.process_pending_lookups();
}

// =============================================================================
// REORG UNDO
// =============================================================================

std::expected<database::utxoz_database::raw_stored, database::result_code>
block_chain::find_utxo_raw(utxoz::raw_outpoint const& key, uint32_t height) const {
    return utxoz_db_.find_raw(key, height);
}

std::pair<boost::unordered_flat_map<utxoz::raw_outpoint, database::utxoz_database::raw_stored>,
          std::vector<utxoz::raw_outpoint>>
block_chain::utxo_process_pending_lookups_raw() {
    return utxoz_db_.process_pending_lookups_raw();
}

#if ! defined(KTH_DB_READONLY)
bool block_chain::store_block_undo(database::header_index::index_t idx,
                                   database::block_undo const& undo,
                                   hash_digest const& prev_hash) {
    // The undo record lives in the rev*.dat file matching the block's blk*.dat,
    // and the header index stores only the offset — so the block must be on disk.
    auto const file_number = header_index_.get_file_number(idx);
    if (file_number < 0) {
        spdlog::error("[blockchain] store_block_undo: no block data for index {}", idx);
        return false;
    }

    auto const pos = block_store_->write_undo(undo, file_number, prev_hash);
    if (pos.is_null()) {
        spdlog::error("[blockchain] store_block_undo: failed to write undo for index {}", idx);
        return false;
    }

    header_index_.set_undo_pos(idx, pos.pos);
    header_index_.add_status(idx, database::header_status::have_undo);
    return true;
}
#endif

std::expected<database::block_undo, database::result_code>
block_chain::read_block_undo(database::header_index::index_t idx, hash_digest const& prev_hash) const {
    if ( ! header_index_.has_undo_data(idx)) {
        return std::unexpected(database::result_code::key_not_found);
    }

    database::flat_file_pos const pos{
        header_index_.get_file_number(idx),
        header_index_.get_undo_pos(idx)
    };
    return block_store_->read_undo(pos, prev_hash);
}

#if ! defined(KTH_DB_READONLY)
database::disconnect_result block_chain::disconnect_block(uint32_t height) {
    // Genesis anchors the chain and has no parent to roll back to.
    if (height == 0) {
        spdlog::error("[blockchain] disconnect_block: refusing to disconnect genesis");
        return database::disconnect_result::failed;
    }

    // Blocks are disconnected newest-first, so `height` must be the validated tip.
    // Disconnecting out of order would restore outputs that a later block still
    // spends, silently corrupting the UTXO set. Fail closed: if the tip cannot be
    // read, we cannot prove this is the tip, so we must not proceed.
    auto const heights = database_.internal_db().get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] disconnect_block: cannot read the validated tip");
        return database::disconnect_result::failed;
    }
    if (height != heights->block) {
        spdlog::error("[blockchain] disconnect_block: height {} is not the validated tip ({})",
            height, heights->block);
        return database::disconnect_result::failed;
    }

    auto const idx = header_index_.active_at(static_cast<int32_t>(height));
    if (idx == database::header_index::null_index) {
        spdlog::error("[blockchain] disconnect_block: height {} is not on the active chain", height);
        return database::disconnect_result::failed;
    }

    if ( ! header_index_.has_block_data(idx)) {
        spdlog::error("[blockchain] disconnect_block: no block data at height {}", height);
        return database::disconnect_result::failed;
    }

    // The outputs the block created are not stored in the undo record: they are
    // recomputed here by re-reading the block, which is why undo data only needs
    // to carry what was spent.
    database::flat_file_pos const pos{
        header_index_.get_file_number(idx),
        header_index_.get_data_pos(idx)
    };
    auto raw = block_store_->read_block_raw(pos);
    if ( ! raw) {
        spdlog::error("[blockchain] disconnect_block: cannot read block at height {}", height);
        return database::disconnect_result::failed;
    }

    auto parsed = parse_utxo_block(byte_span{raw->data(), raw->size()});
    if ( ! parsed) {
        spdlog::error("[blockchain] disconnect_block: cannot parse block at height {}", height);
        return database::disconnect_result::failed;
    }

    auto undo = read_block_undo(idx, header_index_.get_prev_block_hash(idx));
    if ( ! undo) {
        spdlog::error("[blockchain] disconnect_block: no undo data at height {} (block below the "
            "checkpoint, or connected before undo data was recorded)", height);
        return database::disconnect_result::failed;
    }

    // The inverse of a delta is another delta: restore what was spent, remove
    // what was created.
    utxo_raw_delta inverse;
    inverse.inserts.reserve(undo->spent.size());
    inverse.deletes.reserve(parsed->outputs.size());

    for (auto const& entry : undo->spent) {
        inverse.inserts.emplace(entry.key, utxo_raw_value{entry.value, entry.height});
    }
    for (auto const& out : parsed->outputs) {
        inverse.deletes.emplace(out.key, height);
    }

    // An output both created and spent inside this block was never in the set;
    // erasing it is a no-op, and it is absent from the undo record by construction.
    auto const result = utxoz_db_.apply_delta_raw(inverse.inserts, inverse.deletes);
    if (result != database::result_code::success) {
        spdlog::error("[blockchain] disconnect_block: failed to apply inverse delta at height {}", height);
        return database::disconnect_result::unclean;
    }

    // Roll the markers back to the parent block. The UTXO set is already reverted
    // at this point, so a failed marker write leaves the two disagreeing — report
    // it rather than claiming a clean disconnect.
    auto const parent = height - 1;
    auto const block_marker = database_.internal_db().set_last_block_height(parent);
    auto const utxo_marker = database_.internal_db().set_utxo_built_height(parent);
    if (block_marker != database::result_code::success ||
        utxo_marker != database::result_code::success) {
        spdlog::error("[blockchain] disconnect_block: UTXO set rolled back to {} but the height "
            "markers could not be updated", parent);
        return database::disconnect_result::unclean;
    }

    spdlog::info("[blockchain] Disconnected block at height {} ({} outputs removed, {} restored)",
        height, parsed->outputs.size(), undo->spent.size());

    return database::disconnect_result::ok;
}
#endif

#if ! defined(KTH_DB_READONLY)
block_chain::switch_result block_chain::switch_to_branch(
    database::header_index::index_t branch_head, uint32_t fork_height) {

    auto const heights = database_.internal_db().get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] switch_to_branch: cannot read the validated tip");
        return {};
    }

    // Validate the target before touching anything: re-pointing the active chain
    // at a bad index after the UTXO set is already rolled back would leave the
    // node with no coherent chain at all.
    if (branch_head == database::header_index::null_index) {
        spdlog::error("[blockchain] switch_to_branch: null branch head");
        return {false, heights->block};
    }
    if (header_index_.get_height(branch_head) <= int32_t(fork_height)) {
        spdlog::error("[blockchain] switch_to_branch: branch head at height {} is not above the "
            "fork at {}", header_index_.get_height(branch_head), fork_height);
        return {false, heights->block};
    }

    // Do not take the caller's word for where the branch forks: derive it. A
    // fork_height lower than the real one would disconnect fewer blocks than the
    // switch needs, leaving UTXO effects of abandoned blocks active while the
    // active tip moves onto the branch anyway.
    auto const active_tip = header_index_.active_at(header_index_.active_tip_height());
    auto const fork_idx = header_index_.find_fork(branch_head, active_tip);
    if (fork_idx == database::header_index::null_index) {
        spdlog::error("[blockchain] switch_to_branch: branch shares no ancestor with the active chain");
        return {false, heights->block};
    }

    auto const real_fork = header_index_.get_height(fork_idx);
    if (real_fork != int32_t(fork_height)) {
        spdlog::error("[blockchain] switch_to_branch: fork height mismatch — caller says {}, the "
            "branch actually forks at {}", fork_height, real_fork);
        return {false, heights->block};
    }

    // Where the validated tip ends up. A fork above the validated tip disconnects
    // nothing, so the tip does not move — reporting the fork height there would
    // strand the still-missing blocks below it, never requested again.
    uint32_t validated_tip = heights->block;

    if (fork_height > heights->block) {
        spdlog::info("[blockchain] Reorg: fork at {} is above the validated tip {}, nothing to disconnect",
            fork_height, heights->block);
    } else {
        validated_tip = fork_height;

        auto const to_disconnect = heights->block - fork_height;
        spdlog::warn("[blockchain] Reorg: disconnecting {} block(s), {} down to {}",
            to_disconnect, heights->block, fork_height + 1);

        // Newest first: disconnecting out of order would restore outputs that a
        // later block still spends.
        for (uint32_t h = heights->block; h > fork_height; --h) {
            auto const result = disconnect_block(h);

            if (result == database::disconnect_result::unclean) {
                // The inverse delta was applied but the markers could not be
                // written: the UTXO set is at h-1 while the markers still say h.
                // Reporting h as a resumable tip would re-download from a height
                // whose UTXO state is already inconsistent, and nothing would ever
                // repair it. Report "unknown" so the caller stops instead.
                spdlog::error("[blockchain] Reorg: UTXO set and height markers diverged at {} "
                    "(set is at {}, markers say {}); aborting without a resumable tip",
                    h, h - 1, h);
                return {false, std::nullopt};
            }

            if (result != database::disconnect_result::ok) {
                // Clean failure: nothing was applied for this block, so the markers
                // and the set agree at h. Report it so the caller resyncs there
                // instead of assuming nothing moved (which would strand the rewound
                // range, never re-downloaded).
                spdlog::error("[blockchain] Reorg: failed to disconnect block at height {}, "
                    "aborting the switch (validated tip is now {})", h, h);
                return {false, h};
            }
        }
    }

    // The active chain is NOT re-pointed here. Publishing the new tip is the
    // organizer's (see header_organizer::adopt_tip), because the header path
    // publishes there too: a batch that validated against the old tip decides
    // whether to publish under the organizer's lock, and a switch that published
    // the height mapping outside that lock could land between that decision and
    // its write — leaving the tip on one branch and the height mapping on the
    // other. The caller adopts the branch head as soon as this returns.
    spdlog::warn("[blockchain] Reorg: UTXO state rewound to the fork at {}; the branch head is "
        "published by the caller", fork_height);
    return {true, validated_tip};
}
#endif

::asio::awaitable<block_chain::switch_result> block_chain::switch_to_branch_async(
    database::header_index::index_t branch_head, uint32_t fork_height) {

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, switch_result)>;
    result_channel done(co_await ::asio::this_coro::executor, 1);

    ::asio::post(priority_pool_.get_executor(), [this, branch_head, fork_height, &done]() {
        auto result = switch_to_branch(branch_head, fork_height);
        std::ignore = done.try_send(std::error_code{}, result);
    });

    auto [ec, result] = co_await done.async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return switch_result{};
    }
    co_return result;
}

void block_chain::utxo_compact() {
    utxoz_db_.compact();
}

void block_chain::utxo_print_statistics() {
    utxoz_db_.print_statistics();
}

void block_chain::utxo_print_sizing_report() {
    utxoz_db_.print_sizing_report();
}

void block_chain::utxo_print_height_range_stats() {
    utxoz_db_.print_height_range_stats();
}

size_t block_chain::utxo_size() const {
    return utxoz_db_.size();
}

void block_chain::set_utxo_bloom(std::shared_ptr<database::utxo_bloom_filter const> bloom) {
    utxoz_db_.set_utxo_bloom(std::move(bloom));
}

void block_chain::clear_utxo_bloom() {
    utxoz_db_.clear_utxo_bloom();
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

block_validation_store& block_chain::block_validations() const {
    return block_validations_;
}

transaction_validation_store& block_chain::transaction_validations() const {
    return transaction_validations_;
}

namespace {

// Holds the validation mutex at high priority for a scope. The exclusion the
// mempool depends on must not rest on the body never throwing.
struct validation_high_priority_lock {
    explicit validation_high_priority_lock(prioritized_mutex& mutex) : mutex_(mutex) {
        mutex_.lock_high_priority();
    }
    ~validation_high_priority_lock() { mutex_.unlock_high_priority(); }
    validation_high_priority_lock(validation_high_priority_lock const&) = delete;
    validation_high_priority_lock& operator=(validation_high_priority_lock const&) = delete;
private:
    prioritized_mutex& mutex_;
};

} // namespace

code block_chain::mempool_remove_for_block(byte_span raw) {
    validation_high_priority_lock const lock(validation_mutex_);

    // Under the lock, so an admission cannot land between finding the pool empty
    // and deciding there is nothing to do. Empty is the whole of initial sync,
    // where parsing every block for this would be the only reason to parse it.
    if (mempool_.size() == 0) {
        return error::success;
    }

    byte_reader reader(raw);
    auto block = domain::message::block::from_data(reader, 0u);
    if ( ! block) {
        return error::operation_failed;
    }

    mempool_.remove_for_block(*block);
    return error::success;
}

void block_chain::mempool_remove_for_block(domain::chain::block const& block) {
    validation_high_priority_lock const lock(validation_mutex_);
    mempool_.remove_for_block(block);
}

blockchain::mempool& block_chain::mempool_ref() {
    return mempool_;
}

blockchain::mempool const& block_chain::mempool_ref() const {
    return mempool_;
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

std::filesystem::path block_chain::data_dir() const {
    return database_.internal_db_dir.parent_path();
}


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
    return domain::chain::hash(*result);
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
    if (cached) {
        domain::chain::chain_state::ptr state;
        block_validations().visit(cached->hash(), [&](auto const& bv){ state = bv.state; });
        if (state && state->height() == height) {
            co_return std::pair{cached, height};
        }
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
    if (cached) {
        domain::chain::chain_state::ptr state;
        block_validations().visit(cached->hash(), [&](auto const& bv){ state = bv.state; });
        if (state && cached->hash() == hash) {
            co_return std::pair{cached, state->height()};
        }
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
        // Resolve through the active chain — the index also holds side branches,
        // numbered in arrival order, so an entry's index is not its height.
        auto const idx = header_index_.active_at(static_cast<int32_t>(h));

        if (idx == header_index::null_index) {
            spdlog::error("[blockchain] fetch_blocks_raw: Height {} is not on the active chain", h);
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

    co_return std::tuple{domain::chain::hash(*result), result->timestamp(), height};
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

    // Confirmed transactions are no longer indexed by hash (the LMDB transaction
    // store was removed; blocks live in flat files). Reading a tx by hash will be
    // reimplemented over the flat-file block store. See issue #491.
    (void)hash;
    (void)require_confirmed;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<size_t, size_t>>
block_chain::fetch_transaction_position(hash_digest const& hash, bool require_confirmed) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // Confirmed transactions are no longer indexed by hash (the LMDB transaction
    // store was removed; blocks live in flat files). Reading a tx position by hash
    // will be reimplemented over the flat-file block store. See issue #491.
    (void)hash;
    (void)require_confirmed;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<transaction_const_ptr>
block_chain::fetch_unconfirmed_transaction(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const tx = mempool_.get(hash);
    if ( ! tx) {
        co_return std::unexpected(error::not_found);
    }

    co_return tx;
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

    inventory_vector::list inventories;
    inventories.reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            inventories.shrink_to_fit();
            break;
        }
        static auto const id = inventory::type_id::block;
        inventories.emplace_back(id, domain::chain::hash(*result));
    }

    auto hashes = inventory::create(std::move(inventories));
    if ( ! hashes) {
        co_return std::unexpected(hashes.error());
    }

    co_return std::make_shared<inventory>(std::move(*hashes));
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

    domain::message::header::list elements;
    elements.reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            elements.shrink_to_fit();
            break;
        }
        elements.push_back(*result);
    }

    auto message = domain::message::headers::create(std::move(elements));
    if ( ! message) {
        co_return std::unexpected(message.error());
    }

    co_return std::make_shared<domain::message::headers>(std::move(*message));
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
        hashes.push_back(domain::chain::hash(*result));
    }

    co_return message;
}

// The confirmed address index (history / spend) was backed by the LMDB history
// and spend stores, which the v1 node never populates (blocks live in flat files,
// the UTXO set in UTXO-Z). The stores and these queries were removed; a confirmed
// address index will be reintroduced on the v1 model if/when it is needed.

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

// ############################################################################
// TODO(mempool): KTH has no working mempool. The abandoned mining::mempool and
// the LMDB "transaction unconfirmed" storage that used to back these queries
// have been removed to rebuild the mempool from scratch (concurrent hashmap +
// on-disk persistence). Until the new mempool lands, every mempool read/query
// and the tx sink abort loudly (via mempool_not_implemented, defined near the
// top of this file) instead of silently returning wrong (empty) results.
// Tracked in issue #491.
// ############################################################################

awaitable_expected<blockchain::block_template>
block_chain::fetch_template() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const state = chain_state();
    if ( ! state) {
        co_return std::unexpected(error::not_found);
    }

    co_return build_block_template(mempool_, block_template_context{
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        state->height(),
        state->median_time_past()});
}

awaitable_expected<blockchain::mining_template>
block_chain::fetch_mining_template(uint64_t coinbase_reserve_size) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const state = chain_state();
    if ( ! state) {
        co_return std::unexpected(error::not_found);
    }

    auto const height = state->height();

    // Previous block hash = the tip, i.e. the block one below the template height.
    // Together with the mempool generation it forms the cache key.
    hash_digest previous = null_hash;
    if (height > 0) {
        auto const prev = get_block_hash(height - 1);
        if ( ! prev) {
            co_return std::unexpected(error::not_found);
        }
        previous = *prev;
    }

    auto const generation = mempool_.generation();
    auto const now = static_cast<uint32_t>(zulu_time());

    // A snapshot is usable while the tip is unchanged and the mempool is either
    // unchanged or its last change is still within the refresh window; a new tip
    // always forces a rebuild.
    auto const usable = [&](boost::shared_ptr<template_snapshot> const& s) {
        return s &&
               s->previous == previous &&
               s->coinbase_reserve_size == coinbase_reserve_size &&
               (s->generation == generation ||
                now - s->time < settings_.gbt_template_refresh_seconds);
    };

    // Lock-free read: rebuilds are expensive (full mempool scan + fee-rate
    // ordering), so serve the published snapshot when it is still usable.
    auto snapshot = template_cache_.load();
    if (usable(snapshot)) {
        co_return snapshot->value;
    }

    // Stale or cold. Coalesce rebuilds: exactly one thread rebuilds. If another
    // thread already holds the rebuild lock, do not block on it — serve the
    // previous (bounded-stale) snapshot if we have one. Only a cold start with no
    // snapshot at all waits for the first build.
    //
    // NOTE: there is no co_await between here and co_return; the rebuild is fully
    // synchronous, so holding this std::mutex across it is safe. Do not introduce
    // a suspension point inside this section.
    std::unique_lock<std::mutex> lock(template_rebuild_mutex_, std::try_to_lock);
    if ( ! lock) {
        // Another thread is rebuilding. If our stale snapshot is for the SAME tip
        // (only the mempool moved on), it is still a valid template — serve it
        // rather than block. Never serve a snapshot from a previous tip: a
        // wrong-parent template would orphan the miner's block, so wait for the
        // rebuild in that case (a cold start with no snapshot also waits).
        if (snapshot && snapshot->previous == previous) {
            co_return snapshot->value;
        }
        lock.lock();
    }

    // Re-check under the lock: another thread may have published while we waited.
    snapshot = template_cache_.load();
    if (usable(snapshot)) {
        co_return snapshot->value;
    }

    auto selection = build_block_template(mempool_, block_template_context{
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        height,
        state->median_time_past(),
        coinbase_reserve_size});

    // 0x20000000: the BIP9 version base. BCH has no active version-bits signaling,
    // and miners routinely override this, so it is only a sensible default.
    auto built = make_mining_template(
        0x20000000U,
        previous,
        height,
        state->work_required(),
        state->median_time_past(),
        now,
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        std::move(selection));

    auto next = boost::make_shared<template_snapshot>(
        template_snapshot{std::move(built), previous, generation, now, coinbase_reserve_size});
    template_cache_.store(next);
    co_return next->value;
}

awaitable_expected<blockchain::mining_info>
block_chain::fetch_mining_info() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const state = chain_state();
    if ( ! state) {
        co_return std::unexpected(error::not_found);
    }

    auto const heights = co_await fetch_last_height();
    if ( ! heights) {
        co_return std::unexpected(heights.error());
    }

    co_return blockchain::mining_info{
        heights->block,
        difficulty_from_bits(state->work_required()),
        mempool_.size(),
        state->network()};
}

awaitable_expected<inventory_ptr>
block_chain::fetch_mempool(size_t count_limit, uint64_t /*minimum_fee*/) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    inventory_vector::list inventories;
    mempool_.for_each([&](mempool_entry const& e) {
        if (inventories.size() >= count_limit) {
            return;
        }
        inventories.emplace_back(inventory_vector::type_id::transaction, e.tx->hash());
    });

    auto inv = domain::message::inventory::create(std::move(inventories));
    if ( ! inv) {
        co_return std::unexpected(inv.error());
    }

    co_return std::make_shared<domain::message::inventory>(std::move(*inv));
}

hash_list block_chain::get_mempool_txids() const {
    return mempool_.all_txids();
}

blockchain::mempool_totals block_chain::get_mempool_info() const {
    return mempool_.summary();
}

std::optional<mempool_entry_info> block_chain::get_mempool_entry(hash_digest const& txid) const {
    auto const e = mempool_.entry(txid);
    if ( ! e) {
        return std::nullopt;
    }
    return mempool_entry_info{e->fee, e->size, e->time_seen};
}

hash_list block_chain::get_mempool_depends(hash_digest const& txid) const {
    return mempool_.parents(txid);
}

hash_list block_chain::get_mempool_spentby(hash_digest const& txid) const {
    return mempool_.children(txid);
}

hash_list block_chain::get_mempool_ancestors(hash_digest const& txid) const {
    return mempool_.ancestors(txid);
}

hash_list block_chain::get_mempool_descendants(hash_digest const& txid) const {
    return mempool_.descendants(txid);
}

std::filesystem::path block_chain::mempool_dat_path() const {
    return database_.internal_db_dir.parent_path() / "mempool.dat";
}

bool block_chain::dump_mempool_to_disk() const {
    // Snapshot the pool; the database module owns the on-disk format.
    std::vector<database::mempool_stored_tx> txs;
    txs.reserve(mempool_.size());
    mempool_.for_each([&](mempool_entry const& e) {
        txs.push_back({e.tx, e.time_seen});
    });
    return database::store_mempool(mempool_dat_path(), std::move(txs));
}

::asio::awaitable<size_t> block_chain::load_mempool_from_disk() {
    auto const persisted = database::load_mempool(mempool_dat_path());

    size_t admitted = 0;
    for (auto const& p : persisted) {
        // Re-validate against the current tip and admit (recomputes fee/size/
        // sigchecks, enforces first-seen). Now-invalid txs are simply dropped.
        auto const ec = co_await organize(p.tx);
        if ( ! ec) {
            ++admitted;
        }
    }

    if ( ! persisted.empty()) {
        spdlog::info("[blockchain] Re-admitted {}/{} persisted mempool transactions", admitted, persisted.size());
    }
    co_return admitted;
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
        if (auto address = kth::domain::wallet::payment_address::parse_from(payment_address); address) {
            addrs.insert(*address);
        }
    }

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        size_t i = 0;

        for (auto const& output : tx.outputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                output.script(), encoding_p2kh, encoding_p2sh);
            for (auto const tx_address : tx_addresses) {
                if (addrs.find(tx_address) != addrs.end()) {
                    ret.push_back(mempool_transaction_summary(
                        tx_address.encoded_cashaddr(false), kth::encode_hash(tx.hash()), "",
                        "", std::to_string(output.value()), i, e.time_seen));
                }
            }
            ++i;
        }

        // Input-side (debit) entries required resolving each spent prevout's value
        // from the confirmed transaction store, which no longer exists (blocks live
        // in flat files, the UTXO set in UTXO-Z). Only credit (output) entries are
        // reported here until this is reworked over the v1 stores. See issue #491.
    });

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

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        bool inserted = false;

        for (auto iter_output = tx.outputs().begin();
             iter_output != tx.outputs().end() && !inserted; ++iter_output) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_output->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                if (it != payment_addresses.end()) {
                    ret.push_back(tx);
                    inserted = true;
                }
            }
        }

        for (auto iter_input = tx.inputs().begin();
             iter_input != tx.inputs().end() && !inserted; ++iter_input) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_input->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                if (it != payment_addresses.end()) {
                    ret.push_back(tx);
                    inserted = true;
                }
            }
        }
    });

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

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        // BIP-152 short id: the low 6 bytes (little-endian) of the SipHash.
        auto const sh = sip_hash_uint256(k0, k1, tx.hash());
        mini_hash short_id;
        std::memcpy(short_id.data(), &sh, short_id.size());
        mempool.emplace(short_id, tx);
    });

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

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;

        uint64_t shortid = sip_hash_uint256(k0, k1, tx.hash()) & uint64_t(0xffffffffffff);

        auto idit = shorttxids.find(shortid);
        if (idit != shorttxids.end()) {
            if ( ! have_txn[idit->second]) {
                txn_available[idit->second] = tx;
                have_txn[idit->second] = true;
                ++mempool_count;
            } else {
                // A second transaction maps to the same short id, so the slot
                // is ambiguous and gets cleared. A cleared slot holds the null
                // transaction, so clear (and decrement) only once.
                if ( ! txn_available[idit->second].is_null()) {
                    txn_available[idit->second] = domain::chain::transaction::null();
                    --mempool_count;
                }
            }
        }
    });
}

// =============================================================================
// FILTERS
// =============================================================================

::asio::awaitable<code> block_chain::filter_blocks(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    block_organizer_.filter(message);
    auto const& internal_db = database_.internal_db();

    message->erase_if([&internal_db](auto const& inv) {
        return inv.is_block_type() && internal_db.get_header(inv.hash());
    });

    co_return error::success;
}

::asio::awaitable<code> block_chain::filter_transactions(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    // Drop inventory for transactions we already hold in the mempool so we do not
    // re-request them. Confirmed transactions are no longer indexed by hash (the
    // LMDB transaction store was removed); a confirmed-tx filter over the
    // flat-file block store is tracked by issue #491.
    message->erase_if([this](auto const& inv) {
        return inv.is_transaction_type() && mempool_.contains(inv.hash());
    });

    co_return error::success;
}

}} // namespace kth::blockchain
