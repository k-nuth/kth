// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/utxo_builder.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/infrastructure/utility/reader.hpp>

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
    for (auto const& [point, height] : other.deletes) {
        // If it's in our inserts, it's an internal spend within the batch
        auto it = inserts.find(point);
        if (it != inserts.end()) {
            // Internal spend detected - UTXO created and spent within same batch
            // spdlog::debug("[utxo_delta::merge] Internal spend: UTXO created at height {}, spent at height {} - {}:{}",
            //     it->second.height(), height, encode_hash(point.hash()), point.index());
            inserts.erase(it);
        } else {
            // Not in inserts -> real delete from existing UTXO set
            // spdlog::debug("[utxo_delta::merge] External delete: spent at height {} - {}:{}",
            //     height, encode_hash(point.hash()), point.index());
            deletes.emplace(point, height);
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
            auto it = delta.inserts.find(prevout);
            if (it != delta.inserts.end()) {
                // spdlog::debug("[process_block_utxos] Block {} internal spend: {}:{}",
                //     height, encode_hash(prevout.hash()), prevout.index());
                delta.inserts.erase(it);
            } else {
                // Not in inserts -> delete from existing UTXO set
                delta.deletes.emplace(prevout, height);
            }
        }
    }

    return delta;
}

// =============================================================================
// Batch Processing with thread pool (parallel)
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
// Batch Processing sequential (no thread pool)
// =============================================================================

utxo_delta process_blocks_sequential(std::vector<block_with_context> const& blocks) {
    if (blocks.empty()) {
        return utxo_delta{};
    }

    // Process first block
    utxo_delta merged = process_block_utxos(*blocks[0].block, blocks[0].height, blocks[0].median_time_past);

    // Process and merge remaining blocks in order
    for (size_t i = 1; i < blocks.size(); ++i) {
        auto const& ctx = blocks[i];
        auto delta = process_block_utxos(*ctx.block, ctx.height, ctx.median_time_past);
        merged.merge(std::move(delta));
    }

    return merged;
}

// =============================================================================
// Apply Delta to Database
// DEPRECATED: UTXO storage moved to UTXOZ - this function is no longer used
// =============================================================================

database::result_code apply_utxo_delta(
    database::internal_database& db,
    utxo_delta const& delta
) {
    // LMDB UTXO storage removed - use UTXOZ instead via block_chain::apply_utxo_delta
    (void)db;
    (void)delta;
    spdlog::warn("[utxo_builder] apply_utxo_delta(internal_database) is deprecated - use block_chain::apply_utxo_delta instead");
    return database::result_code::other;
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

// Format number with k/m suffix
[[nodiscard]]
std::string fmt_num(size_t n) {
    if (n >= 1'000'000) {
        return fmt::format("{}m", n / 1'000'000);
    }
    if (n >= 1'000) {
        return fmt::format("{}k", n / 1'000);
    }
    return fmt::format("{}", n);
}

} // anonymous namespace

// Helper to get strategy name for logging
[[nodiscard]]
char const* strategy_name(utxo_build_strategy strategy) {
    switch (strategy) {
        case utxo_build_strategy::parallel_batch: return "parallel_batch";
        case utxo_build_strategy::sequential_batch: return "sequential_batch";
        case utxo_build_strategy::sequential_direct: return "sequential_direct";
    }
    return "unknown";
}

// Helper to process deferred deletions and report errors
[[nodiscard]]
database::result_code process_deferred_deletions(block_chain& chain) {
    auto deferred = chain.utxo_deferred_deletions_size();
    if (deferred > 0) {
        spdlog::debug("[utxo_builder] Processing {} pending deletions...", deferred);
        auto [deleted, failed] = chain.utxo_process_pending_deletions();
        if ( ! failed.empty()) {
            spdlog::error("[utxo_builder] FATAL: Deferred deletions failed: {} deleted, {} failed",
                deleted, failed.size());
            for (auto const& entry : failed) {
                auto point = database::utxoz_database::key_to_point(entry.key);
                spdlog::error("[utxo_builder] Failed to delete UTXO at block {}: {}:{}",
                    entry.height, encode_hash(point.hash()), point.index());
            }
            return database::result_code::key_not_found;
        }
    }
    return database::result_code::success;
}

::asio::awaitable<database::result_code> build_utxo_set(
    block_chain& chain,
    ::asio::thread_pool& pool,
    uint32_t start_height,
    uint32_t end_height,
    utxo_build_strategy strategy
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

    spdlog::info("[utxo_builder] Building UTXO set from height {} to {} (strategy: {})",
        actual_start, end_height, strategy_name(strategy));

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

    // ==========================================================================
    // Strategy: sequential_direct - process 1 block at a time, apply directly
    // ==========================================================================
    if (strategy == utxo_build_strategy::sequential_direct) {
        for (uint32_t h = actual_start; h <= end_height; ++h) {
            auto block_result = co_await chain.fetch_block(h);
            if (!block_result) {
                spdlog::error("[utxo_builder] Failed to fetch block at height {}", h);
                co_return database::result_code::other;
            }

            uint32_t mtp = calculate_mtp(timestamp_window);
            auto& [block_ptr, height] = *block_result;

            // Process single block
            auto delta = process_block_utxos(*block_ptr, h, mtp);

            // Apply directly to UTXO-Z
            auto result = chain.apply_utxo_delta(delta.inserts, delta.deletes);
            if (result != database::result_code::success) {
                spdlog::error("[utxo_builder] Failed to apply UTXO delta at height {}", h);
                co_return result;
            }

            // Update timestamp window
            if (timestamp_window.size() >= mtp_interval) {
                timestamp_window.pop_front();
            }
            timestamp_window.push_back(block_ptr->header().timestamp());

            ++processed;

            // Every batch_size blocks: process pending deletions, save progress, log
            if (processed % batch_size == 0 || h == end_height) {
                // Process deferred deletions
                auto deferred_result = process_deferred_deletions(chain);
                if (deferred_result != database::result_code::success) {
                    co_return deferred_result;
                }

                // Save progress
                auto save_result = chain.set_utxo_built_height(h);
                if (save_result != database::result_code::success) {
                    spdlog::warn("[utxo_builder] Failed to save progress at height {}", h);
                }

                double pct = (100.0 * processed) / total_blocks;
                spdlog::info("[utxo_builder] Progress: {}/{} blocks ({:.1f}%)",
                    processed, total_blocks, pct);
            }
        }
    }
    // ==========================================================================
    // Strategy: parallel_batch or sequential_batch - process batch, then apply
    // ==========================================================================
    else {
        using clock = std::chrono::steady_clock;

        for (uint32_t batch_start = actual_start; batch_start <= end_height; batch_start += batch_size) {
            uint32_t batch_end = std::min(batch_start + static_cast<uint32_t>(batch_size) - 1, end_height);

            // ===== TIMING: Fetch raw blocks (LMDB read only) =====
            auto t_fetch_start = clock::now();

            auto raw_result = chain.fetch_blocks_raw(batch_start, batch_end);
            if (!raw_result) {
                spdlog::error("[utxo_builder] Failed to fetch blocks {}-{}", batch_start, batch_end);
                co_return database::result_code::other;
            }

            auto t_fetch_end = clock::now();
            auto fetch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_fetch_end - t_fetch_start).count();

            // ===== TIMING: Deserialize blocks =====
            auto t_deser_start = clock::now();

            domain::chain::block::list blocks;
            blocks.reserve(raw_result->size());
            for (auto const& raw_data : *raw_result) {
                byte_reader reader(raw_data);
                auto block_res = domain::chain::block::from_data(reader);
                if (!block_res) {
                    spdlog::error("[utxo_builder] Failed to deserialize block in batch {}-{}", batch_start, batch_end);
                    co_return database::result_code::other;
                }
                blocks.push_back(std::move(*block_res));
            }

            auto t_deser_end = clock::now();
            auto deser_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_deser_end - t_deser_start).count();

            // Build context with MTP
            std::vector<block_with_context> blocks_ctx;
            blocks_ctx.reserve(blocks.size());

            for (size_t i = 0; i < blocks.size(); ++i) {
                uint32_t h = batch_start + static_cast<uint32_t>(i);
                uint32_t mtp = calculate_mtp(timestamp_window);

                blocks_ctx.push_back({
                    .block = &blocks[i],
                    .height = h,
                    .median_time_past = mtp
                });

                // Update timestamp window
                if (timestamp_window.size() >= mtp_interval) {
                    timestamp_window.pop_front();
                }
                timestamp_window.push_back(blocks[i].header().timestamp());
            }

            // ===== TIMING: Process batch (delta calculation) =====
            auto t_process_start = clock::now();

            utxo_delta delta;
            if (strategy == utxo_build_strategy::parallel_batch) {
                delta = co_await process_blocks_parallel(pool, blocks_ctx);
            } else {
                // sequential_batch
                delta = process_blocks_sequential(blocks_ctx);
            }

            auto t_process_end = clock::now();
            auto process_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_process_end - t_process_start).count();

            // ===== TIMING: Apply to UTXO-Z =====
            auto t_apply_start = clock::now();

            auto result = chain.apply_utxo_delta(delta.inserts, delta.deletes);
            if (result != database::result_code::success) {
                spdlog::error("[utxo_builder] Failed to apply UTXO delta at batch starting {}", batch_start);
                co_return result;
            }

            auto t_apply_end = clock::now();
            auto apply_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_apply_end - t_apply_start).count();

            // Count deferred deletions added during apply
            auto deferred_count = chain.utxo_deferred_deletions_size();

            // ===== TIMING: Deferred deletions =====
            auto t_deferred_start = clock::now();

            auto deferred_result = process_deferred_deletions(chain);
            if (deferred_result != database::result_code::success) {
                co_return deferred_result;
            }

            auto t_deferred_end = clock::now();
            auto deferred_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_deferred_end - t_deferred_start).count();

            // Save progress after each successful batch
            auto save_result = chain.set_utxo_built_height(batch_end);
            if (save_result != database::result_code::success) {
                spdlog::warn("[utxo_builder] Failed to save progress at height {}", batch_end);
            }

            processed += blocks_ctx.size();
            double pct = (100.0 * processed) / total_blocks;
            auto total_ms = fetch_ms + deser_ms + process_ms + apply_ms + deferred_ms;
            auto direct_del = delta.delete_count() - deferred_count;

            spdlog::info("[utxo_builder] {}/{} ({:.1f}%) | {}ms (f:{} s:{} p:{} a:{} d:{}) | {} ins, ({},{}) del",
                processed, total_blocks, pct,
                total_ms, fetch_ms, deser_ms, process_ms, apply_ms, deferred_ms,
                fmt_num(delta.insert_count()), fmt_num(direct_del), fmt_num(deferred_count));
            spdlog::debug("[utxo_builder] {}/{} ({:.1f}%) | {}ms (f:{} s:{} p:{} a:{} d:{}) | {} ins, ({},{}) del",
                processed, total_blocks, pct,
                total_ms, fetch_ms, deser_ms, process_ms, apply_ms, deferred_ms,
                delta.insert_count(), direct_del, deferred_count);
        }
    }

    // Final compaction after all batches
    spdlog::info("[utxo_builder] Running final compaction...");
    chain.utxo_compact();

    spdlog::info("[utxo_builder] UTXO set build complete: {} blocks processed", processed);
    co_return database::result_code::success;
}

} // namespace kth::blockchain
