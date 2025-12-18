// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync_session.hpp>

#include <algorithm>
#include <latch>

#include <kth/infrastructure/utility/system_memory.hpp>
#include <kth/network/protocols_coro.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

namespace kth::node {

using namespace kth::blockchain;
using namespace kth::network;

// =============================================================================
// Construction
// =============================================================================

sync_session::sync_session(
    block_chain& chain,
    peer_session::ptr peer,
    domain::config::network network,
    sync_config const& config)
    : chain_(chain)
    , organizer_(chain, chain.chain_settings(), network)
    , peer_(std::move(peer))
    , config_(config)
{
    // Get current chain heights (header-sync and block-sync)
    auto const heights = chain_.get_last_heights();
    if (heights) {
        header_height_ = heights->first;   // header-sync height
        block_height_ = heights->second;   // block-sync height
    }

    // Get target height from peer's version message
    auto peer_version = peer_->peer_version();
    if (peer_version) {
        target_height_ = peer_version->start_height();
    }

    // Get hash of current header tip (for getheaders locator)
    if (header_height_ > 0) {
        auto const hash = chain_.get_block_hash(header_height_);
        if (hash) {
            last_header_hash_ = *hash;
        }
    } else {
        // Genesis block hash
        auto const genesis_header = chain_.get_header(0);
        if (genesis_header) {
            last_header_hash_ = genesis_header->hash();
        }
    }
}

sync_session::ptr make_sync_session(
    block_chain& chain,
    peer_session::ptr peer,
    domain::config::network network,
    sync_config const& config)
{
    return std::make_shared<sync_session>(chain, peer, network, config);
}

// =============================================================================
// Run
// =============================================================================

::asio::awaitable<sync_result> sync_session::run() {
    sync_result result{};

    spdlog::info("[sync] Starting headers-first sync with [{}], header-sync: {}, block-sync: {}",
        peer_->authority(), header_height_, block_height_);

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

    auto const headers_to_sync = target_height_ > header_height_
        ? target_height_ - header_height_ : 0;

    if (headers_to_sync > 0) {
        spdlog::info("[sync] Phase 1: Syncing headers from [{}], target height {}, need ~{} headers",
            peer_->authority(), target_height_, headers_to_sync);

        // Log memory allocator info
        if (kth::is_jemalloc_active()) {
            spdlog::info("[sync] Memory allocator: jemalloc {}", kth::get_jemalloc_version());
        } else {
            spdlog::info("[sync] Memory allocator: system default");
        }

        // Log system memory info
        auto const total_mem = kth::get_total_system_memory();
        auto const available_mem = kth::get_available_system_memory();
        spdlog::info("[sync] System memory: total {} MB, available {} MB",
            total_mem / (1024 * 1024), available_mem / (1024 * 1024));

        // Initialize organizer with current state and expected headers
        organizer_.start();
        organizer_.initialize(header_height_, last_header_hash_, headers_to_sync);

        auto const optimal_cache = organizer_.calculate_optimal_cache_size(headers_to_sync);
        spdlog::info("[sync] Header cache: optimal size {} headers", optimal_cache);

        // Measure memory before sync
        auto const mem_before = kth::get_resident_memory();
        spdlog::info("[sync] Memory before header sync: {} MB", mem_before / (1024 * 1024));

        auto const start_time = std::chrono::steady_clock::now();

        // Sync headers until complete
        while (!peer_->stopped() && !synced_) {
            auto ec = co_await sync_headers_batch();
            if (ec) {
                if (ec == error::channel_timeout) {
                    spdlog::debug("[sync] Headers timeout from [{}], retrying...",
                        peer_->authority());
                    continue;
                }
                result.error = ec;
                co_return result;
            }

            // Log progress every 10000 headers
            if (total_headers_ % 10000 == 0 && total_headers_ > 0) {
                auto const elapsed = std::chrono::steady_clock::now() - start_time;
                auto const elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                auto const headers_per_sec = elapsed_secs > 0 ? total_headers_ / elapsed_secs : 0;

                auto const current_synced_height = header_height_ + total_headers_;
                auto const hash_str = encode_hash(last_header_hash_);

                if (target_height_ > 0) {
                    auto const percent = (current_synced_height * 100) / target_height_;
                    auto const remaining = headers_to_sync > total_headers_ ? headers_to_sync - total_headers_ : 0;
                    auto const eta_secs = headers_per_sec > 0 ? remaining / headers_per_sec : 0;

                    spdlog::info("[sync] Headers: {:>7}/{} ({:>2}%), {:>6}/s, ETA {:>3}s, cache: {} headers",
                        current_synced_height, target_height_, percent,
                        headers_per_sec, eta_secs,
                        organizer_.cache_size());
                } else {
                    spdlog::info("[sync] Headers: {:>7}, {:>6}/s, cache: {} headers",
                        current_synced_height, headers_per_sec,
                        organizer_.cache_size());
                }
            }
        }

        // Measure memory after headers received
        auto const mem_after_headers = kth::get_resident_memory();
        auto const bytes_per_header = total_headers_ > 0
            ? (mem_after_headers - mem_before) / total_headers_ : 0;
        spdlog::info("[sync] Memory after {} headers: {} MB (delta: {} MB, ~{} bytes/header)",
            total_headers_, mem_after_headers / (1024 * 1024),
            (mem_after_headers - mem_before) / (1024 * 1024), bytes_per_header);

        // Flush remaining headers to database
        if (organizer_.has_pending()) {
            spdlog::info("[sync] Flushing {} cached headers to database...",
                organizer_.cache_size());
            auto const db_start = std::chrono::steady_clock::now();

            auto const flush_ec = organizer_.flush();
            if (flush_ec && flush_ec != error::duplicate_block) {
                spdlog::warn("[sync] Failed to flush headers: {}", flush_ec.message());
                result.error = flush_ec;
                co_return result;
            }

            auto const db_elapsed = std::chrono::steady_clock::now() - db_start;
            auto const db_ms = std::chrono::duration_cast<std::chrono::milliseconds>(db_elapsed).count();
            spdlog::info("[sync] Headers flush completed in {}ms", db_ms);
        }

        auto const final_header_height = organizer_.header_height();
        spdlog::info("[sync] Phase 1 complete: synced to height {} ({} new headers) from [{}]",
            final_header_height, total_headers_, peer_->authority());

        // Clean up organizer
        organizer_.clear_cache();
        organizer_.stop();

    } else {
        spdlog::info("[sync] Phase 1: Headers already synced to height {}", header_height_);
    }

    // ==========================================================================
    // PHASE 2: Download blocks
    // ==========================================================================

    auto const current_header_height = header_height_ + total_headers_;
    if (block_height_ < current_header_height) {
        // We need to download blocks - get hashes from DB
        std::vector<hash_digest> block_hashes;

        if (block_height_ < current_header_height) {
            auto const blocks_to_download = current_header_height - block_height_;
            spdlog::info("[sync] Loading {} block hashes from database...", blocks_to_download);

            block_hashes.reserve(blocks_to_download);
            for (size_t h = block_height_ + 1; h <= current_header_height; ++h) {
                auto const hash = chain_.get_block_hash(h);
                if (hash) {
                    block_hashes.push_back(*hash);
                }
            }
        }

        if (!block_hashes.empty()) {
            spdlog::info("[sync] Phase 2: Downloading blocks {}-{} ({} blocks) from [{}]",
                block_height_ + 1, block_height_ + block_hashes.size(),
                block_hashes.size(), peer_->authority());

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
    auto locator = build_locator();

    spdlog::debug("[sync] Requesting headers from [{}] with {} locator hashes",
        peer_->authority(), locator.size());

    // Request headers
    auto headers_result = co_await request_headers(
        *peer_, locator, null_hash, config_.headers_timeout);

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
    auto const add_result = organizer_.add_headers(headers.elements());
    if (add_result.error) {
        spdlog::warn("[sync] Invalid headers from [{}]: {}",
            peer_->authority(), add_result.error.message());
        co_return add_result.error;
    }

    // Update tracking state
    total_headers_ += add_result.headers_added;
    last_header_hash_ = organizer_.header_tip_hash();

    spdlog::debug("[sync] Added {} headers, cache now {} headers ({} MB)",
        add_result.headers_added, add_result.cache_size,
        add_result.cache_memory_bytes / (1024 * 1024));

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

    // Start with the tip (from organizer if we have cached headers)
    auto const tip_hash = organizer_.header_tip_hash();
    if (tip_hash != null_hash) {
        locator.push_back(tip_hash);
    } else if (last_header_hash_ != null_hash) {
        locator.push_back(last_header_hash_);
    }

    // Add exponentially spaced hashes going back
    size_t step = 1;
    size_t height = header_height_ + total_headers_;

    while (height > 0) {
        if (height <= step) {
            height = 0;
        } else {
            height -= step;
        }

        auto const hash = chain_.get_block_hash(height);
        if (hash) {
            locator.push_back(*hash);
        }

        if (locator.size() >= 10) {
            step *= 2;
        }

        if (locator.size() >= 64) {
            break;
        }
    }

    // Always include genesis
    if (height > 0) {
        auto const genesis_hash = chain_.get_block_hash(0);
        if (genesis_hash) {
            locator.push_back(*genesis_hash);
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
    return {organizer_.header_height(), block_height_};
}

// =============================================================================
// Utility Functions
// =============================================================================

::asio::awaitable<sync_result> sync_from_best_peer(
    block_chain& chain,
    p2p_node& p2p,
    domain::config::network network,
    sync_config const& config)
{
    sync_result result{};

    // Get current chain heights
    size_t our_block_height = 0;
    auto const heights = chain.get_last_heights();
    if (heights) {
        our_block_height = heights->second;
    }

    // Get all connected peers
    auto peers = co_await p2p.peers().all();

    if (peers.empty()) {
        spdlog::warn("[sync] No peers available for sync");
        result.error = error::channel_stopped;
        co_return result;
    }

    spdlog::info("[sync] Selecting sync peer from {} connected peers, our height: {}",
        peers.size(), our_block_height);

    // Find peer with highest start_height that's ahead of us
    peer_session::ptr best_peer;
    uint32_t best_height = 0;

    for (auto const& peer : peers) {
        auto version = peer->peer_version();
        if (!version) {
            continue;
        }

        auto const peer_height = version->start_height();

        if (peer_height <= our_block_height) {
            continue;
        }

        if (peer_height > best_height) {
            best_height = peer_height;
            best_peer = peer;
        }
    }

    if (!best_peer) {
        spdlog::info("[sync] No peers ahead of us (height {}), already synced?", our_block_height);
        result.error = error::success;
        result.final_height = our_block_height;
        co_return result;
    }

    spdlog::info("[sync] Selected peer [{}] with height {} for sync",
        best_peer->authority(), best_height);

    auto session = make_sync_session(chain, best_peer, network, config);
    co_return co_await session->run();
}

} // namespace kth::node
