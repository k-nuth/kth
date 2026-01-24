// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/utxo_builder.hpp>

#include <algorithm>
#include <atomic>
#include <deque>
#include <vector>

#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::blockchain {

// =============================================================================
// utxo_delta implementation
// =============================================================================

void utxo_delta::merge(utxo_delta&& other) {
    // 1. Add other's inserts to our inserts
    for (auto& [point, entry] : other.inserts) {
        inserts.emplace(point, std::move(entry));
    }

    // 2. Process other's deletes
    for (auto const& point : other.deletes) {
        // If it's in our inserts, it's an internal spend within the batch
        if (inserts.erase(point) == 0) {
            // Not in inserts -> real delete from existing UTXO set
            deletes.insert(point);
        }
    }
}

void utxo_delta::clear() {
    inserts.clear();
    deletes.clear();
}

bool utxo_delta::empty() const {
    return inserts.empty() && deletes.empty();
}

// =============================================================================
// Block Processing
// =============================================================================

utxo_delta process_block_utxos(
    domain::chain::block const& block,
    uint32_t height,
    uint32_t median_time_past
) {
    utxo_delta delta;

    auto const& txs = block.transactions();
    if (txs.empty()) {
        return delta;
    }

    // 1. FIRST: Process all outputs (inserts)

    // Coinbase transaction
    auto const& coinbase = txs.front();
    auto const coinbase_hash = coinbase.hash();
    uint32_t idx = 0;
    for (auto const& output : coinbase.outputs()) {
        delta.inserts.emplace(
            domain::chain::point{coinbase_hash, idx++},
            database::utxo_entry{output, height, median_time_past, true}
        );
    }

    // Non-coinbase transactions - outputs only
    for (size_t i = 1; i < txs.size(); ++i) {
        auto const& tx = txs[i];
        auto const tx_hash = tx.hash();
        idx = 0;
        for (auto const& output : tx.outputs()) {
            delta.inserts.emplace(
                domain::chain::point{tx_hash, idx++},
                database::utxo_entry{output, height, median_time_past, false}
            );
        }
    }

    // 2. THEN: Process all inputs (deletes)
    // Skip coinbase (index 0) - it has no real inputs
    for (size_t i = 1; i < txs.size(); ++i) {
        auto const& tx = txs[i];
        for (auto const& input : tx.inputs()) {
            auto const& prevout = input.previous_output();

            // If in inserts, it's an internal spend within this block
            if (delta.inserts.erase(prevout) == 0) {
                // Not in inserts -> delete from existing UTXO set
                delta.deletes.insert(prevout);
            }
        }
    }

    return delta;
}

// =============================================================================
// Batch Processing with thread pool
// =============================================================================

::asio::awaitable<utxo_delta> process_blocks_parallel(
    ::asio::thread_pool& pool,
    std::vector<block_with_context> const& blocks
) {
    if (blocks.empty()) {
        co_return utxo_delta{};
    }

    auto const n = blocks.size();

    // Storage for results (each slot written by exactly one worker)
    std::vector<utxo_delta> results(n);

    // Atomic counter to track completion
    auto completed = std::make_shared<std::atomic<size_t>>(0);

    // Post each block processing to the thread pool
    for (size_t i = 0; i < n; ++i) {
        auto const& ctx = blocks[i];
        ::asio::post(pool, [&ctx, &results, completed, i]() {
            results[i] = process_block_utxos(*ctx.block, ctx.height, ctx.median_time_past);
            completed->fetch_add(1, std::memory_order_release);
        });
    }

    // Wait for all workers to complete (yield-friendly polling)
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    while (completed->load(std::memory_order_acquire) < n) {
        timer.expires_after(std::chrono::microseconds(100));
        co_await timer.async_wait(::asio::use_awaitable);
    }

    // Merge all results in order (sequential)
    utxo_delta merged = std::move(results[0]);
    for (size_t i = 1; i < n; ++i) {
        merged.merge(std::move(results[i]));
    }

    co_return merged;
}

// =============================================================================
// Apply Delta to Database
// =============================================================================

database::result_code apply_utxo_delta(
    database::internal_database& db,
    utxo_delta const& delta
) {
    spdlog::info("[utxo_builder] Applying delta: {} inserts, {} deletes",
        delta.insert_count(), delta.delete_count());

    // Delegate to database method which handles transaction properly
    return db.apply_utxo_delta(delta.inserts, delta.deletes);
}

// =============================================================================
// UTXO Set Builder
// =============================================================================

namespace {

using block_const_ptr = domain::message::block::const_ptr;

constexpr size_t mtp_interval = 11;
constexpr size_t batch_size = 1000;

// Calculate median of timestamps (MTP algorithm)
[[nodiscard]]
uint32_t calculate_mtp(std::deque<uint32_t> const& timestamps) {
    if (timestamps.empty()) {
        return 0;
    }

    // Copy and sort
    std::vector<uint32_t> sorted(timestamps.begin(), timestamps.end());
    std::sort(sorted.begin(), sorted.end());

    // Return middle element (integer division gives floor)
    return sorted[sorted.size() / 2];
}

} // anonymous namespace

::asio::awaitable<database::result_code> build_utxo_set(
    block_chain& chain,
    ::asio::thread_pool& pool,
    uint32_t start_height,
    uint32_t end_height
) {
    // Check if we have a saved progress point
    auto saved_height = chain.get_utxo_built_height();
    uint32_t actual_start = start_height;

    if (saved_height && *saved_height >= start_height) {
        actual_start = *saved_height + 1;
        spdlog::info("[utxo_builder] Resuming UTXO build from height {} (was at {})",
            actual_start, *saved_height);
    }

    if (actual_start > end_height) {
        spdlog::info("[utxo_builder] UTXO set already built up to height {}", end_height);
        co_return database::result_code::success;
    }

    spdlog::info("[utxo_builder] Building UTXO set from height {} to {}", actual_start, end_height);

    // Track last 11 timestamps for MTP calculation
    std::deque<uint32_t> timestamp_window;

    // Pre-load timestamps for blocks before actual_start (up to 11)
    uint32_t preload_start = (actual_start > mtp_interval) ? (actual_start - mtp_interval) : 0;
    for (uint32_t h = preload_start; h < actual_start; ++h) {
        auto header_result = chain.get_header(h);
        if (header_result) {
            timestamp_window.push_back(header_result->timestamp());
        }
    }

    size_t total_blocks = end_height - actual_start + 1;
    size_t processed = 0;

    for (uint32_t batch_start = actual_start; batch_start <= end_height; batch_start += batch_size) {
        uint32_t batch_end = std::min(batch_start + static_cast<uint32_t>(batch_size) - 1, end_height);

        // Storage for blocks in this batch (must outlive processing)
        std::vector<block_const_ptr> block_storage;
        std::vector<block_with_context> blocks_ctx;
        block_storage.reserve(batch_end - batch_start + 1);
        blocks_ctx.reserve(batch_end - batch_start + 1);

        // Fetch blocks and calculate MTP for each
        for (uint32_t h = batch_start; h <= batch_end; ++h) {
            auto block_result = co_await chain.fetch_block(h);
            if (!block_result) {
                spdlog::error("[utxo_builder] Failed to fetch block at height {}", h);
                co_return database::result_code::other;
            }

            // Calculate MTP before adding this block's timestamp
            uint32_t mtp = calculate_mtp(timestamp_window);

            auto& [block_ptr, height] = *block_result;
            block_storage.push_back(block_ptr);
            blocks_ctx.push_back({
                .block = block_ptr.get(),
                .height = h,
                .median_time_past = mtp
            });

            // Update timestamp window
            if (timestamp_window.size() >= mtp_interval) {
                timestamp_window.pop_front();
            }
            timestamp_window.push_back(block_ptr->header().timestamp());
        }

        // Process batch in parallel
        auto delta = co_await process_blocks_parallel(pool, blocks_ctx);

        // Apply delta to database
        auto result = chain.apply_utxo_delta(delta.inserts, delta.deletes);
        if (result != database::result_code::success) {
            spdlog::error("[utxo_builder] Failed to apply UTXO delta at batch starting {}", batch_start);
            co_return result;
        }

        // Save progress after each successful batch
        auto save_result = chain.set_utxo_built_height(batch_end);
        if (save_result != database::result_code::success) {
            spdlog::warn("[utxo_builder] Failed to save progress at height {}", batch_end);
        }

        processed += blocks_ctx.size();
        double pct = (100.0 * processed) / total_blocks;
        spdlog::info("[utxo_builder] Progress: {}/{} blocks ({:.1f}%) - {} inserts, {} deletes",
            processed, total_blocks, pct, delta.insert_count(), delta.delete_count());
    }

    spdlog::info("[utxo_builder] UTXO set build complete: {} blocks processed", processed);
    co_return database::result_code::success;
}

} // namespace kth::blockchain
