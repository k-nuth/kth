// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync_session.hpp>

#include <algorithm>
#include <latch>

#include <kth/infrastructure/utility/stats.hpp>
#include <kth/infrastructure/utility/system_memory.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/parallel_sync_v2.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::node {

using namespace kth::blockchain;
using namespace kth::network;

// "Permanent" ban duration - 10 years in seconds
// Using a large but safe value instead of hours::max() to avoid overflow
constexpr auto permanent_ban_duration = std::chrono::seconds(10 * 365 * 24 * 60 * 60);

// Forward declaration - defined at end of file
static ::asio::awaitable<void> persist_headers_background(
    block_chain& chain,
    header_index const& index,
    size_t start_height,
    size_t end_height);

// =============================================================================
// Construction
// =============================================================================

sync_session::sync_session(
    block_chain& chain,
    peer_session::ptr peer,
    domain::config::network network,
    sync_config const& config,
    size_t target_height,
    p2p_node* p2p_node)
    : chain_(chain)
    , organizer_(chain.headers(), chain.chain_settings(), network)
    , peer_(std::move(peer))
    , p2p_node_(p2p_node)
    , config_(config)
{
    // Get current chain heights (header-sync and block-sync)
    auto const heights = chain_.get_last_heights();
    if (heights) {
        header_height_ = heights->first;   // header-sync height
        block_height_ = heights->second;   // block-sync height
    }

    // Use provided target height if given, otherwise use peer's start_height
    if (target_height > 0) {
        target_height_ = target_height;
    } else {
        auto peer_version = peer_->peer_version();
        if (peer_version) {
            target_height_ = peer_version->start_height();
        }
    }

    // Get hash of current header tip (for getheaders locator)
    if (header_height_ > 0) {
        // TODO(fernando): resuming sync - headers in DB need to be loaded into header_index
        auto const hash = chain_.get_block_hash(header_height_);
        if (hash) {
            last_header_hash_ = *hash;
        }
    } else {
        // Fresh start - get genesis hash
        auto const genesis = chain_.get_header(0);
        if (genesis) {
            last_header_hash_ = genesis->hash();
        }
    }
}

sync_session::ptr make_sync_session(
    block_chain& chain,
    peer_session::ptr peer,
    domain::config::network network,
    sync_config const& config,
    size_t target_height,
    p2p_node* p2p_node)
{
    return std::make_shared<sync_session>(chain, peer, network, config, target_height, p2p_node);
}

// =============================================================================
// Run
// =============================================================================

::asio::awaitable<sync_result> sync_session::run() {
    sync_result result{};

    spdlog::info("[sync] Starting headers-first sync with [{}], header-sync: {}, block-sync: {}",
        peer_->authority_with_agent(), header_height_, block_height_);

    // Check peer version supports headers-first
    if (peer_->negotiated_version() < config_.minimum_version) {
        spdlog::warn("[sync] Peer [{}] version {} too low for headers-first sync",
            peer_->authority(), peer_->negotiated_version());
        result.error = error::channel_stopped;
        co_return result;
    }

    // ==========================================================================
    // PHASE 1: Sync ALL headers first
    // ==========================================================================

    // Sync organizer tip from header_index FIRST (may have headers from previous peer)
    organizer_.start();
    organizer_.sync_tip();

    // Use organizer's height (from shared header_index) instead of DB height
    auto const current_header_height = organizer_.header_height();
    auto const headers_to_sync = target_height_ > uint32_t(current_header_height)
        ? target_height_ - current_header_height : 0;

    if (headers_to_sync > 0) {
        spdlog::info("[sync] Phase 1: Syncing headers from [{}], target height {}, current {}, need ~{} headers",
            peer_->authority(), target_height_, current_header_height, headers_to_sync);

        // // Log memory allocator info
        // if (kth::is_jemalloc_active()) {
        //     spdlog::info("[sync] Memory allocator: jemalloc {}", kth::get_jemalloc_version());
        // } else {
        //     spdlog::info("[sync] Memory allocator: system default");
        // }

        // Log system memory info
        // auto const total_mem = kth::get_total_system_memory();
        // auto const available_mem = kth::get_available_system_memory();
        // spdlog::info("[sync] System memory: total {} MB, available {} MB",
        //     total_mem / (1024 * 1024), available_mem / (1024 * 1024));

        // BCHN-style: Calculate headers sync deadline
        // Timeout = base + per_header * expected_headers
        headers_sync_start_ = clock::now();
        auto const timeout_duration = config_.headers_download_timeout_base +
            std::chrono::duration_cast<std::chrono::seconds>(
                config_.headers_download_timeout_per_header * headers_to_sync);
        headers_sync_deadline_ = headers_sync_start_ + timeout_duration;

        auto const timeout_secs = std::chrono::duration_cast<std::chrono::seconds>(timeout_duration).count();
        spdlog::info("[sync] Headers sync timeout: {}s (base: {}s + {}us/header * {} headers)",
            timeout_secs,
            config_.headers_download_timeout_base.count(),
            config_.headers_download_timeout_per_header.count(),
            headers_to_sync);

        // TODO(fernando): calculate optimal cache size based on available memory
        // auto const optimal_cache = organizer_.calculate_optimal_cache_size(headers_to_sync);
        // spdlog::info("[sync] Header cache: optimal size {} headers", optimal_cache);

        // Measure memory before sync
        // auto const mem_before = kth::get_resident_memory();
        // spdlog::info("[sync] Memory before header sync: {} MB", mem_before / (1024 * 1024));

        auto const start_time = std::chrono::steady_clock::now();
        int consecutive_timeouts = 0;
        constexpr int max_consecutive_timeouts = 3;

        // Sync headers until complete
        while (!peer_->stopped() && !synced_) {
            // BCHN-style: Check if we've exceeded the headers sync deadline
            if (clock::now() > headers_sync_deadline_) {
                auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    clock::now() - headers_sync_start_).count();
                spdlog::warn("[sync] Headers sync timeout after {}s from [{}], "
                    "received {} headers (peer too slow)",
                    elapsed, peer_->authority(), total_headers_);
                headers_sync_timed_out_ = true;
                result.error = error::channel_timeout;
                result.timed_out = true;
                co_return result;
            }

            auto ec = co_await sync_headers_batch();
            if (ec) {
                if (ec == error::channel_timeout) {
                    ++consecutive_timeouts;
                    if (consecutive_timeouts >= max_consecutive_timeouts) {
                        spdlog::warn("[sync] {} consecutive batch timeouts from [{}], giving up on this peer",
                            consecutive_timeouts, peer_->authority());
                        result.error = error::channel_timeout;
                        result.timed_out = true;
                        co_return result;
                    }
                    spdlog::debug("[sync] Headers batch timeout from [{}], retrying ({}/{})...",
                        peer_->authority(), consecutive_timeouts, max_consecutive_timeouts);
                    continue;
                }
                result.error = ec;
                co_return result;
            }

            // Reset timeout counter on success
            consecutive_timeouts = 0;

            // Log progress every 10000 headers
            if (total_headers_ % 10000 == 0 && total_headers_ > 0) {
                auto const elapsed = std::chrono::steady_clock::now() - start_time;
                auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                auto const headers_per_sec = elapsed_secs > 0 ? total_headers_ / elapsed_secs : 0;

                // Use organizer's current height (includes headers from previous peers)
                auto const current_synced_height = organizer_.header_height();

                if (target_height_ > 0) {
                    auto const percent = (uint32_t(current_synced_height) * 100) / target_height_;
                    auto const remaining = headers_to_sync > total_headers_ ? headers_to_sync - total_headers_ : 0;
                    auto const eta_secs = headers_per_sec > 0 ? remaining / headers_per_sec : 0;

                    spdlog::info("[sync] {:>7}/{} ({:>2}%), {:>6}/s, ETA {:>3}s",
                        current_synced_height, target_height_, percent,
                        headers_per_sec, eta_secs);

#ifdef KTH_WITH_STATS
                    // Print detailed stats when enabled
                    auto const& stats = global_sync_stats();
                    auto const batch_cnt = stats.batch_count.load(std::memory_order_relaxed);
                    if (batch_cnt > 0) {
                        auto const loc_ms = stats.batch_locator_ns.load() / 1000000 / batch_cnt;
                        auto const net_ms = stats.batch_network_ns.load() / 1000000 / batch_cnt;
                        auto const proc_ms = stats.batch_process_ns.load() / 1000000 / batch_cnt;
                        spdlog::info("[stats] loc:{}ms net:{}ms proc:{}ms/batch", loc_ms, net_ms, proc_ms);
                    }
#endif
                } else {
                    spdlog::info("[sync] {:>7}, {:>6}/s, index: {} headers",
                        current_synced_height, headers_per_sec,
                        organizer_.size());
                }
            }
        }

        // Measure memory after headers received
        // auto const mem_after_headers = kth::get_resident_memory();
        // auto const bytes_per_header = total_headers_ > 0
        //     ? (mem_after_headers - mem_before) / total_headers_ : 0;
        // spdlog::info("[sync] Memory after {} headers: {} MB (delta: {} MB, ~{} bytes/header)",
        //     total_headers_, mem_after_headers / (1024 * 1024),
        //     (mem_after_headers - mem_before) / (1024 * 1024), bytes_per_header);

        // NOTE: Header persistence to DB now happens in Phase 2 as a background task
        // This allows block sync to proceed immediately while headers are saved

        auto const final_header_height = organizer_.header_height();

        // Check why header sync ended
        if (!synced_ && peer_->stopped()) {
            spdlog::warn("[sync] Header sync peer disconnected at height {} (target was {})",
                final_header_height, target_height_);
        }

        spdlog::info("[sync] Phase 1 complete: synced to height {} ({} new headers) from [{}]",
            final_header_height, total_headers_, peer_->authority());

        organizer_.stop();

    } else {
        spdlog::info("[sync] Phase 1: Headers already synced to height {}", header_height_);
    }

    // ==========================================================================
    // PHASE 2: Download blocks (with parallel header persistence)
    // ==========================================================================

    auto const final_header_height = organizer_.header_height();

    // Spawn background task to persist headers to database
    // This runs in parallel with block sync - headers are in memory for block validation
    // but also being written to DB so restart won't require re-syncing headers
    {
        // Get current DB header height to determine what needs persisting
        auto const db_heights = chain_.get_last_heights();
        size_t db_header_height = db_heights ? db_heights->first : 0;

        // Only persist if we have new headers beyond what's in DB
        if (final_header_height > 0 && size_t(final_header_height) > db_header_height) {
            auto executor = co_await ::asio::this_coro::executor;

            // Spawn persist task in background (detached)
            // Uses references to chain_ and index which outlive sync_session
            ::asio::co_spawn(executor,
                persist_headers_background(
                    chain_,
                    organizer_.index(),
                    db_header_height + 1,
                    static_cast<size_t>(final_header_height)),
                ::asio::detached);
        }
    }

    if (block_height_ < final_header_height) {
        auto const blocks_to_download = final_header_height - block_height_;
        spdlog::info("[sync] Phase 2: Need to download {} blocks ({} to {})",
            blocks_to_download, block_height_ + 1, final_header_height);

        // Use parallel block sync if p2p_node is available
        if (p2p_node_ != nullptr) {
            spdlog::info("[sync] Phase 2: Using parallel block download from multiple peers");

            // Convert sync_config to parallel_download_config_v2 (lock-free coordinator)
            parallel_download_config_v2 parallel_config{
                .chunk_size = 16,  // Blocks per chunk (Bitcoin protocol limit)
                .slots_multiplier = 100,  // slots_per_round = max_peers * 100
                .max_peers = 8,
                .stall_timeout = std::chrono::seconds(config_.block_stalling_timeout),
                .timeout_check_interval = std::chrono::seconds(5)
            };

            auto parallel_result = co_await parallel_block_sync_v2(
                chain_,
                organizer_,
                *p2p_node_,
                static_cast<uint32_t>(block_height_ + 1),
                static_cast<uint32_t>(final_header_height),
                parallel_config);

            if (parallel_result.error) {
                spdlog::error("[sync] Parallel block sync failed: {}",
                    parallel_result.error.message());
                result.error = parallel_result.error;
                co_return result;
            }

            total_blocks_ = parallel_result.blocks_validated;
            block_height_ = parallel_result.final_height;

            spdlog::info("[sync] Phase 2 complete: {} blocks validated via parallel sync",
                parallel_result.blocks_validated);

        } else {
            // Fallback to sequential download from single peer
            spdlog::info("[sync] Phase 2: Using sequential block download from [{}]",
                peer_->authority());

            std::vector<hash_digest> block_hashes;
            block_hashes.reserve(blocks_to_download);

            // During IBD, headers are added in order, so index == height for main chain
            auto& idx = organizer_.index();
            for (size_t h = block_height_ + 1; h <= size_t(final_header_height); ++h) {
                block_hashes.push_back(idx.get_hash(h));
            }

            auto ec = co_await sync_blocks(block_hashes);
            if (ec) {
                result.error = ec;
                co_return result;
            }
        }
    }

    result.headers_received = total_headers_;
    result.blocks_received = total_blocks_;
    result.final_height = block_height_;

    spdlog::info("[sync] Sync complete with [{}], height {} ({} headers, {} blocks)",
        peer_->authority(), block_height_, total_headers_, total_blocks_);

    co_return result;
}

// =============================================================================
// Headers Sync
// =============================================================================

::asio::awaitable<code> sync_session::sync_headers_batch() {
    // Build locator from current chain state
    KTH_STATS_TIME_START(locator);
    auto locator = build_locator();
    KTH_STATS_TIME_ADD(global_sync_stats(), locator, batch_locator_ns);

    spdlog::debug("[sync] Requesting headers from [{}] with {} locator hashes",
        peer_->authority(), locator.size());

    // Request headers
    KTH_STATS_TIME_START(network);
    auto headers_result = co_await request_headers(
        *peer_, locator, null_hash, config_.headers_timeout);
    KTH_STATS_TIME_ADD(global_sync_stats(), network, batch_network_ns);

    if (!headers_result) {
        spdlog::debug("[sync] Failed to get headers from [{}]: {}",
            peer_->authority(), headers_result.error().message());
        co_return headers_result.error();
    }

    auto const& headers = *headers_result;
    auto const count = headers.elements().size();

    spdlog::debug("[sync] Received {} headers from [{}]",
        count, peer_->authority());

    // Empty response means we're synced
    if (count == 0) {
        spdlog::info("[sync] Peer returned 0 headers - marking as synced");
        synced_ = true;
        co_return error::success;
    }

    // Add headers to organizer (validates and caches)
    KTH_STATS_TIME_START(process);
    auto const add_result = organizer_.add_headers(headers.elements());
    KTH_STATS_TIME_ADD(global_sync_stats(), process, batch_process_ns);

    // Increment batch count once per successful batch
    KTH_STATS_INCREMENT(global_sync_stats(), batch_count);

    if (add_result.error) {
        spdlog::warn("[sync] Invalid headers from [{}]: {}",
            peer_->authority(), add_result.error.message());
        co_return add_result.error;
    }

    // Update tracking state
    total_headers_ += add_result.headers_added;
    last_header_hash_ = organizer_.header_tip_hash();

    spdlog::debug("[sync] Added {} headers, index now {} headers ({} MB)",
        add_result.headers_added, add_result.index_size,
        add_result.index_memory_bytes / (1024 * 1024));

    // If we got fewer than max headers (2000), peer has no more
    if (count < config_.max_headers_per_request) {
        synced_ = true;
        spdlog::info("[sync] Headers sync complete: received {} < {} (max batch) - total: {}",
            count, config_.max_headers_per_request, total_headers_);
    }

    co_return error::success;
}

// =============================================================================
// Blocks Sync
// =============================================================================

::asio::awaitable<code> sync_session::sync_blocks(
    std::vector<hash_digest> const& hashes)
{
    auto const total_to_download = hashes.size();
    auto const start_height = block_height_;
    auto const target_height = start_height + total_to_download;

    spdlog::info("[sync] Starting block download: blocks {}-{} ({} blocks) from [{}]",
        start_height + 1, target_height, total_to_download, peer_->authority());

    auto const start_time = std::chrono::steady_clock::now();

    // Request blocks in batches
    for (size_t i = 0; i < hashes.size(); i += config_.max_blocks_per_request) {
        auto const batch_end = std::min(i + config_.max_blocks_per_request, hashes.size());

        for (size_t j = i; j < batch_end; ++j) {
            auto block_result = co_await request_block(
                *peer_, hashes[j], config_.block_timeout);

            if (!block_result) {
                spdlog::debug("[sync] Failed to get block {} from [{}]: {}",
                    encode_hash(hashes[j]), peer_->authority(),
                    block_result.error().message());
                co_return block_result.error();
            }

            auto block_ptr = std::make_shared<domain::message::block>(
                std::move(*block_result));

            spdlog::debug("[sync] Organizing block {} at height {}",
                encode_hash(hashes[j]).substr(0, 16), block_height_ + 1);

            auto ec = co_await chain_.organize(block_ptr);

            if (ec && ec != error::duplicate_block) {
                spdlog::warn("[sync] Block organize failed for {} from [{}]: {}",
                    encode_hash(hashes[j]), peer_->authority(), ec.message());
            }

            ++block_height_;
            ++total_blocks_;

            // Log progress every 1000 blocks
            if (total_blocks_ % 1000 == 0) {
                auto const elapsed = std::chrono::steady_clock::now() - start_time;
                auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                auto const blocks_per_sec = elapsed_secs > 0 ? total_blocks_ / elapsed_secs : 0;
                auto const remaining = total_to_download - total_blocks_;
                auto const eta_secs = blocks_per_sec > 0 ? remaining / blocks_per_sec : 0;

                spdlog::info("[sync] Blocks: {}/{} ({}%), height {}, {}/s, ETA {}s",
                    total_blocks_, total_to_download,
                    (total_blocks_ * 100) / total_to_download,
                    block_height_,
                    blocks_per_sec,
                    eta_secs);
            }
        }
    }

    auto const elapsed = std::chrono::steady_clock::now() - start_time;
    auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    spdlog::info("[sync] Block download complete: {} blocks in {}s ({}/s)",
        total_blocks_, elapsed_secs,
        elapsed_secs > 0 ? total_blocks_ / elapsed_secs : total_blocks_);

    co_return error::success;
}

// =============================================================================
// Locator Building
// =============================================================================

hash_list sync_session::build_locator() const {
    hash_list locator;

    // Use header_index for headers we've synced (much faster than DB)
    auto const& index = organizer_.index();
    auto const tip_idx = organizer_.tip_index();

    if (tip_idx == blockchain::header_index::null_index) {
        // No headers synced yet, just use genesis from DB
        auto const genesis_hash = chain_.get_block_hash(0);
        if (genesis_hash) {
            locator.push_back(*genesis_hash);
        }
        return locator;
    }

    // Start with tip
    locator.push_back(index.get_hash(tip_idx));

    // Use skip pointers for O(log n) traversal to build exponentially-spaced locator
    size_t step = 1;
    auto current_idx = tip_idx;
    int32_t current_height = index.get_height(tip_idx);

    while (current_height > 0 && locator.size() < 64) {
        int32_t target_height = current_height > static_cast<int32_t>(step)
            ? current_height - static_cast<int32_t>(step)
            : 0;

        // Use O(log n) ancestor lookup via skip pointers
        auto ancestor_idx = index.get_ancestor(current_idx, target_height);
        if (ancestor_idx == blockchain::header_index::null_index) {
            break;
        }

        locator.push_back(index.get_hash(ancestor_idx));
        current_idx = ancestor_idx;
        current_height = target_height;

        if (locator.size() >= 10) {
            step *= 2;
        }
    }

    // Always include genesis if not already included
    if (current_height > 0) {
        auto genesis_idx = index.get_ancestor(current_idx, 0);
        if (genesis_idx != blockchain::header_index::null_index) {
            locator.push_back(index.get_hash(genesis_idx));
        }
    }

    return locator;
}

// =============================================================================
// Properties
// =============================================================================

bool sync_session::is_synced() const {
    return synced_;
}

std::pair<size_t, size_t> sync_session::current_heights() const {
    auto const h = organizer_.header_height();
    return {h >= 0 ? size_t(h) : 0, block_height_};
}

// =============================================================================
// Utility Functions
// =============================================================================

/// Select the best peer for sync using BCHN logic:
/// 1. Prefer "preferred download" peers (fPreferredDownload in BCHN)
/// 2. If no preferred peers available, use any full node that's ahead of us
/// 3. Among candidates, prefer the one with highest reported height
///
/// BCHN's logic (net_processing.cpp):
///   fPreferredDownload = (!fInbound || HasPermission(PF_NOBAN))
///                        && !fOneShot && !fClient;
///   fFetch = fPreferredDownload || (nPreferredDownload == 0 && !fClient && !fOneShot);
static peer_session::ptr select_sync_peer(
    std::vector<peer_session::ptr> const& peers,
    size_t our_height)
{
    peer_session::ptr best_preferred = nullptr;
    peer_session::ptr best_fallback = nullptr;
    uint32_t best_preferred_height = 0;
    uint32_t best_fallback_height = 0;

    for (auto const& peer : peers) {
        if (peer->stopped()) {
            continue;
        }

        auto version = peer->peer_version();
        if (!version) {
            continue;
        }

        auto const height = version->start_height();

        // Only consider peers ahead of us
        if (height <= our_height) {
            continue;
        }

        // BCHN: fPreferredDownload = (!fInbound || PF_NOBAN) && !fOneShot && !fClient
        if (peer->is_preferred_download()) {
            if (height > best_preferred_height) {
                best_preferred = peer;
                best_preferred_height = height;
            }
        } else if (!peer->is_one_shot() && !peer->is_client()) {
            // BCHN fallback: nPreferredDownload == 0 && !fClient && !fOneShot
            if (height > best_fallback_height) {
                best_fallback = peer;
                best_fallback_height = height;
            }
        }
    }

    // Prefer "preferred download" peer if available
    if (best_preferred) {
        spdlog::debug("[sync] Selected preferred peer with height {} (outbound or whitelisted full node)",
            best_preferred_height);
        return best_preferred;
    }

    // Fall back to any full node if no preferred peers available
    // (BCHN: nPreferredDownload == 0 && !fClient && !fOneShot)
    if (best_fallback) {
        spdlog::debug("[sync] Selected fallback peer with height {} (no preferred peers available)",
            best_fallback_height);
        return best_fallback;
    }

    return nullptr;
}

::asio::awaitable<sync_result> sync_from_best_peer(
    block_chain& chain,
    p2p_node& p2p,
    domain::config::network network,
    sync_config const& config)
{
    sync_result result{};

    // Get current chain heights - use header index for sync selection
    // During headers-first sync, header index > persisted headers > blocks
    size_t our_block_height = 0;
    size_t our_db_header_height = 0;
    auto const heights = chain.get_last_heights();
    if (heights) {
        our_db_header_height = heights->first;
        our_block_height = heights->second;
    }
    // Header index may have more headers than persisted to DB
    size_t our_header_height = chain.headers().size();
    if (our_header_height > 0) {
        --our_header_height;  // size() is count, height is count-1
    }

    // For peer selection:
    // - If we need blocks (block_height < header_height), use block_height
    //   so we select peers that can give us blocks
    // - If headers and blocks are synced, use header_height for new blocks
    size_t our_sync_height = (our_block_height < our_header_height)
        ? our_block_height   // Need blocks - select peers ahead of our block height
        : std::max({our_header_height, our_db_header_height, our_block_height});

    // BCHN-style: don't wait for minimum peers, start sync immediately
    // when any suitable peer is available

    // Get all connected peers
    auto peers = co_await p2p.peers().all();

    if (peers.empty()) {
        spdlog::warn("[sync] No peers available for sync");
        result.error = error::channel_stopped;
        co_return result;
    }

    // Peers below max checkpoint are useless for sync
    auto const max_checkpoint_height = chain.chain_settings().max_checkpoint_height;

    // Filter out peers below max checkpoint
    auto const initial_peer_count = peers.size();
    peers.erase(
        std::remove_if(peers.begin(), peers.end(),
            [max_checkpoint_height](auto const& peer) {
                auto version = peer->peer_version();
                return !version || size_t(version->start_height()) < max_checkpoint_height;
            }),
        peers.end());

    if (peers.empty()) {
        spdlog::warn("[sync] No peers at or above checkpoint height {} (filtered {} peers)",
            max_checkpoint_height, initial_peer_count);
        result.error = error::channel_stopped;
        co_return result;
    }

    if (peers.size() < initial_peer_count) {
        spdlog::debug("[sync] Filtered {} peers below checkpoint height {}",
            initial_peer_count - peers.size(), max_checkpoint_height);
    }

    // Count peers by type for logging
    size_t preferred_count = 0;
    size_t full_node_count = 0;
    for (auto const& peer : peers) {
        if (peer->is_preferred_download()) {
            ++preferred_count;
        }
        if (peer->is_full_node()) {
            ++full_node_count;
        }
    }

    // Calculate max height across all peers (BCHN-style)
    // Don't trust just one peer's reported height
    size_t max_peer_height = 0;
    for (auto const& peer : peers) {
        auto version = peer->peer_version();
        if (version) {
            max_peer_height = std::max(max_peer_height, size_t(version->start_height()));
        }
    }

    spdlog::info("[sync] Selecting sync peer from {} peers ({} preferred, {} full nodes), "
        "our height: {} (headers: {}, blocks: {}), checkpoint: {}, max peer: {}",
        peers.size(), preferred_count, full_node_count,
        our_sync_height, our_header_height, our_block_height, max_checkpoint_height, max_peer_height);

    // Select best peer using BCHN logic
    auto best_peer = select_sync_peer(peers, our_sync_height);

    if (!best_peer) {
        // Check if there are peers with higher height that we couldn't sync from
        // (e.g., all were banned or filtered out)
        if (max_peer_height > our_sync_height) {
            spdlog::info("[sync] No suitable peers found among {} remaining (none ahead of height {})",
                peers.size(), our_sync_height);
            result.error = error::operation_failed;
            result.final_height = our_sync_height;
            co_return result;
        }
        // Truly synced - no peers have higher height
        spdlog::info("[sync] No peers ahead of us (height {}), already synced?", our_sync_height);
        result.error = error::success;
        result.final_height = our_sync_height;
        co_return result;
    }

    auto version = best_peer->peer_version();
    auto const best_height = version ? version->start_height() : 0;

    spdlog::info("[sync] Selected {} peer [{}] with height {} for sync (target: {})",
        best_peer->is_preferred_download() ? "preferred" : "fallback",
        best_peer->authority_with_agent(), best_height, max_peer_height);

    // Run sync with selected peer, using max_peer_height as target
    // Pass p2p_node to enable parallel block download
    auto session = make_sync_session(chain, best_peer, network, config, max_peer_height, &p2p);
    result = co_await session->run();

    // BCHN-style: If sync failed (including timeout), try another peer
    // Timeout means peer is too slow - disconnect and try another
    if (result.error) {
        if (result.timed_out) {
            spdlog::warn("[sync] Peer [{}] timed out (too slow), disconnecting and trying another...",
                best_peer->authority_with_agent());
            // BCHN disconnects slow peers
            best_peer->stop(error::channel_timeout);
        } else if (result.error == error::checkpoints_failed) {
            // BCHN bans peers that send headers failing checkpoint validation
            // This typically indicates a peer on a different chain (BSV, etc.)
            spdlog::warn("[sync] Peer [{}] sent headers failing checkpoint (wrong chain?), banning permanently...",
                best_peer->authority_with_agent());
            p2p.ban_peer(best_peer, permanent_ban_duration, network::ban_reason::checkpoint_failed);
        } else if (result.error == error::store_block_missing_parent) {
            // Headers don't connect to our chain - peer is likely on a different chain
            spdlog::warn("[sync] Peer [{}] sent headers that don't connect (wrong chain?), banning permanently...",
                best_peer->authority_with_agent());
            p2p.ban_peer(best_peer, permanent_ban_duration, network::ban_reason::checkpoint_failed);
        } else {
            spdlog::warn("[sync] Sync with [{}] failed: {}, trying another peer...",
                best_peer->authority_with_agent(), result.error.message());
        }

        // Remove failed peer from consideration and try again
        peers.erase(
            std::remove(peers.begin(), peers.end(), best_peer),
            peers.end());

        spdlog::debug("[sync] {} peers remaining after removing failed peer", peers.size());

        // Retry with remaining peers until we succeed or run out of peers
        int retry = 0;
        while (!peers.empty()) {
            auto retry_peer = select_sync_peer(peers, our_sync_height);
            if (!retry_peer) {
                spdlog::info("[sync] No suitable peers found among {} remaining (none ahead of height {})",
                    peers.size(), our_sync_height);
                break;
            }

            ++retry;
            version = retry_peer->peer_version();
            auto const retry_height = version ? version->start_height() : 0;
            spdlog::info("[sync] Retry {}: {} peer [{}] height {} (target: {})",
                retry,
                retry_peer->is_preferred_download() ? "preferred" : "fallback",
                retry_peer->authority_with_agent(), retry_height, max_peer_height);

            auto retry_session = make_sync_session(chain, retry_peer, network, config, max_peer_height, &p2p);
            result = co_await retry_session->run();

            if (!result.error) {
                break;  // Success!
            }

            if (result.timed_out) {
                spdlog::warn("[sync] Retry peer [{}] timed out, disconnecting...",
                    retry_peer->authority_with_agent());
                retry_peer->stop(error::channel_timeout);
            } else if (result.error == error::checkpoints_failed) {
                // Ban peers that fail checkpoint (wrong chain)
                spdlog::warn("[sync] Retry peer [{}] sent headers failing checkpoint (wrong chain?), banning permanently...",
                    retry_peer->authority_with_agent());
                p2p.ban_peer(retry_peer, permanent_ban_duration, network::ban_reason::checkpoint_failed);
            } else if (result.error == error::store_block_missing_parent) {
                // Headers don't connect - wrong chain
                spdlog::warn("[sync] Retry peer [{}] sent headers that don't connect (wrong chain?), banning permanently...",
                    retry_peer->authority_with_agent());
                p2p.ban_peer(retry_peer, permanent_ban_duration, network::ban_reason::checkpoint_failed);
            }

            // Remove this peer and try next
            peers.erase(
                std::remove(peers.begin(), peers.end(), retry_peer),
                peers.end());
        }
    }

    co_return result;
}

// =============================================================================
// Header Persistence (Background Task)
// =============================================================================

// Free function for header persistence - captures only what it needs
// so it can safely run after sync_session is destroyed
static ::asio::awaitable<void> persist_headers_background(
    block_chain& chain,
    header_index const& index,
    size_t start_height,
    size_t end_height)
{
    if (start_height > end_height) {
        spdlog::debug("[sync] No headers to persist (start {} > end {})", start_height, end_height);
        co_return;
    }

    auto const total_to_persist = end_height - start_height + 1;
    spdlog::info("[sync] Starting background header persistence: {} headers ({} to {})",
        total_to_persist, start_height, end_height);

    auto const persist_start = std::chrono::steady_clock::now();

    // Persist in batches to avoid holding large vectors in memory
    constexpr size_t batch_size = 1000;

    size_t persisted = 0;
    size_t height = start_height;

    while (height <= end_height) {
        // Build batch of headers
        domain::chain::header::list batch;
        auto const batch_end = std::min(height + batch_size - 1, end_height);
        batch.reserve(batch_end - height + 1);

        for (size_t h = height; h <= batch_end; ++h) {
            // During IBD, index == height (headers added in order)
            batch.push_back(index.get_header(static_cast<uint32_t>(h)));
        }

        // Persist batch to database
        auto const ec = chain.organize_headers_batch(batch, height);
        if (ec) {
            spdlog::warn("[sync] Header persistence failed at height {}: {}",
                height, ec.message());
            // Don't abort - headers are still in memory for block sync
            co_return;
        }

        persisted += batch.size();
        height = batch_end + 1;

        // Log progress every 50000 headers
        if (persisted % 50000 < batch_size) {
            auto const elapsed = std::chrono::steady_clock::now() - persist_start;
            auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            auto const rate = elapsed_secs > 0 ? persisted / elapsed_secs : persisted;
            spdlog::info("[sync] Header persistence progress: {}/{} ({}/s)",
                persisted, total_to_persist, rate);
        }

        // Yield to allow other coroutines to run
        co_await ::asio::post(co_await ::asio::this_coro::executor, ::asio::use_awaitable);
    }

    auto const elapsed = std::chrono::steady_clock::now() - persist_start;
    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    spdlog::info("[sync] Header persistence complete: {} headers in {}ms",
        persisted, elapsed_ms);
}

// Member function wrapper (for declaration compatibility)
::asio::awaitable<void> sync_session::persist_headers_to_db(size_t start_height, size_t end_height) {
    co_await persist_headers_background(chain_, organizer_.index(), start_height, end_height);
}

} // namespace kth::node
