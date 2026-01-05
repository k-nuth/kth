// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/block_download_coordinator.hpp>

#include <algorithm>

#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

namespace kth::node {

block_download_coordinator::block_download_coordinator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    uint32_t start_height,
    uint32_t target_height,
    ::asio::any_io_executor executor,
    parallel_download_config const& config)
    : config_(config)
    , chain_(chain)
    , organizer_(organizer)
    , start_height_(start_height)
    , target_height_(target_height)
    , strand_(::asio::make_strand(executor))
    , next_height_to_assign_(start_height)
    , next_height_to_validate_(start_height)
    , start_time_(std::chrono::steady_clock::now())
    , validation_queue_(std::make_unique<validation_channel>(executor, config.global_window))
{
    spdlog::debug("[coordinator] Created for blocks {}-{} ({} blocks)",
        start_height, target_height, target_height - start_height + 1);
}

block_download_coordinator::~block_download_coordinator() {
    stop();
}

// =============================================================================
// Peer Interface
// =============================================================================

::asio::awaitable<std::vector<std::pair<uint32_t, hash_digest>>> block_download_coordinator::claim_blocks(
    peer_ptr const& peer,
    size_t max_count)
{
    // Execute on strand to serialize access to shared state
    co_return co_await ::asio::co_spawn(strand_, [this, peer, max_count]() -> ::asio::awaitable<std::vector<std::pair<uint32_t, hash_digest>>> {
        if (stopped_ || failed_) {
            co_return std::vector<std::pair<uint32_t, hash_digest>>{};
        }

        // Count how many blocks this peer already has in-flight
        size_t peer_in_flight = 0;
        for (auto const& [height, assignment] : in_flight_) {
            if (assignment.peer == peer) {
                ++peer_in_flight;
            }
        }

        // Limit per peer
        if (peer_in_flight >= config_.max_blocks_per_peer) {
            spdlog::debug("[coordinator] Peer [{}] at limit ({} >= {} max)",
                peer->authority(), peer_in_flight, config_.max_blocks_per_peer);
            co_return std::vector<std::pair<uint32_t, hash_digest>>{};
        }

        size_t can_claim = std::min(max_count, config_.max_blocks_per_peer - peer_in_flight);

        // Check in-flight limit (don't count pending - those are already downloaded)
        size_t total_in_flight = in_flight_.size();
        if (total_in_flight >= config_.global_window) {
            spdlog::debug("[coordinator] In-flight window full for peer [{}]: {} >= {} window",
                peer->authority(), total_in_flight, config_.global_window);
            co_return std::vector<std::pair<uint32_t, hash_digest>>{};
        }

        can_claim = std::min(can_claim, config_.global_window - total_in_flight);

        std::vector<std::pair<uint32_t, hash_digest>> result;
        result.reserve(can_claim);

        auto now = std::chrono::steady_clock::now();

        while (result.size() < can_claim && next_height_to_assign_ <= target_height_) {
            uint32_t height = next_height_to_assign_++;

            // Get hash from header organizer
            auto hash = get_block_hash(height);
            if (hash == null_hash) {
                spdlog::warn("[coordinator] No header hash for height {}", height);
                set_failed(error::not_found);
                break;
            }

            // Track assignment
            in_flight_[height] = {peer, now};
            result.emplace_back(height, hash);
        }

        if (!result.empty()) {
            spdlog::debug("[coordinator] Peer [{}] claimed {} blocks ({}-{})",
                peer->authority(), result.size(),
                result.front().first, result.back().first);
        }

        co_return result;
    }, ::asio::use_awaitable);
}

::asio::awaitable<void> block_download_coordinator::block_received(
    uint32_t height,
    hash_digest const& hash,
    block_const_ptr block)
{
    // Execute on strand to serialize access to shared state
    co_await ::asio::co_spawn(strand_, [this, height, hash, block = std::move(block)]() -> ::asio::awaitable<void> {
        if (stopped_ || failed_) {
            co_return;
        }

        // Remove from in-flight
        auto it = in_flight_.find(height);
        if (it != in_flight_.end()) {
            in_flight_.erase(it);
        }

        // Store in pending buffer
        pending_blocks_[height] = std::move(block);

        spdlog::trace("[coordinator] Block {} received, {} pending, next to validate: {}",
            height, pending_blocks_.size(), next_height_to_validate_);

        // Try to push ready blocks to validation
        flush_pending_to_validation();
    }, ::asio::use_awaitable);
}

::asio::awaitable<void> block_download_coordinator::peer_disconnected(peer_ptr const& peer) {
    // Execute on strand to serialize access to shared state
    co_await ::asio::co_spawn(strand_, [this, peer]() -> ::asio::awaitable<void> {
        // Find and remove all blocks assigned to this peer
        std::vector<uint32_t> to_reassign;
        for (auto const& [height, assignment] : in_flight_) {
            if (assignment.peer == peer) {
                to_reassign.push_back(height);
            }
        }

        for (auto height : to_reassign) {
            in_flight_.erase(height);
        }

        if (!to_reassign.empty()) {
            // Reset assignment pointer to allow these blocks to be reclaimed
            // Find the minimum height that was in-flight
            uint32_t min_height = *std::min_element(to_reassign.begin(), to_reassign.end());
            if (min_height < next_height_to_assign_) {
                next_height_to_assign_ = min_height;
            }

            spdlog::info("[coordinator] Peer [{}] disconnected, reassigning {} blocks from height {}",
                peer->authority(), to_reassign.size(), min_height);
        }
    }, ::asio::use_awaitable);
}

// =============================================================================
// Validation Pipeline
// =============================================================================

::asio::awaitable<std::optional<std::pair<uint32_t, block_download_coordinator::block_const_ptr>>>
block_download_coordinator::next_block_to_validate()
{
    if (stopped_ || failed_) {
        co_return std::nullopt;
    }

    // Try to receive from channel
    auto [ec, block_pair] = co_await validation_queue_->async_receive(
        ::asio::as_tuple(::asio::use_awaitable));

    if (ec) {
        // Channel closed or error
        co_return std::nullopt;
    }

    co_return block_pair;
}

void block_download_coordinator::validation_complete(uint32_t height, code result) {
    if (result != error::success) {
        spdlog::error("[coordinator] Validation failed at height {}: {}",
            height, result.message());
        set_failed(result);
        return;
    }

    ++blocks_validated_;

    // Check if we're done
    if (blocks_validated_.load() >= (target_height_ - start_height_ + 1)) {
        spdlog::info("[coordinator] All {} blocks validated",
            target_height_ - start_height_ + 1);
        validation_queue_->close();
    }
}

// =============================================================================
// Status & Control
// =============================================================================

void block_download_coordinator::check_timeouts() {
    // Post to strand to serialize access
    ::asio::post(strand_, [this]() {
        if (stopped_ || failed_) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        std::vector<uint32_t> stalled;

        for (auto const& [height, assignment] : in_flight_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - assignment.requested_at);

            if (elapsed > config_.stall_timeout) {
                spdlog::warn("[coordinator] Block {} stalled from [{}] ({}s)",
                    height, assignment.peer->authority(), elapsed.count());
                stalled.push_back(height);
            }
        }

        // Remove stalled assignments (will be reclaimed)
        for (auto height : stalled) {
            in_flight_.erase(height);
        }

        if (!stalled.empty()) {
            // Reset assignment to lowest stalled height
            uint32_t min_height = *std::min_element(stalled.begin(), stalled.end());
            if (min_height < next_height_to_assign_) {
                next_height_to_assign_ = min_height;
            }
            spdlog::info("[coordinator] {} stalled blocks reassigned from height {}",
                stalled.size(), min_height);
        }
    });
}

bool block_download_coordinator::is_complete() const {
    return blocks_validated_.load() >= (target_height_ - start_height_ + 1);
}

bool block_download_coordinator::has_failed() const {
    return failed_.load();
}

bool block_download_coordinator::is_stopped() const {
    return stopped_.load();
}

code block_download_coordinator::failure_reason() const {
    return failure_reason_;
}

void block_download_coordinator::stop() {
    if (stopped_.exchange(true)) {
        return;  // Already stopped
    }

    if (validation_queue_) {
        validation_queue_->close();
    }
}

block_download_coordinator::progress block_download_coordinator::get_progress() const {
    // This is called from validation pipeline which is on the strand,
    // so we can safely read without additional synchronization.
    // For extra safety, we use atomics for frequently changing values.

    // Count unique peers with blocks in-flight
    boost::unordered_flat_set<peer_ptr> unique_peers;
    for (auto const& [height, assignment] : in_flight_) {
        unique_peers.insert(assignment.peer);
    }

    return {
        .blocks_downloaded = uint32_t(
            (next_height_to_assign_ - start_height_) - in_flight_.size() + pending_blocks_.size()),
        .blocks_validated = blocks_validated_.load(),
        .blocks_in_flight = uint32_t(in_flight_.size()),
        .blocks_pending = uint32_t(pending_blocks_.size()),
        .active_peers = uint32_t(unique_peers.size()),
        .start_height = start_height_,
        .target_height = target_height_,
        .start_time = start_time_
    };
}

// =============================================================================
// Private Helpers
// =============================================================================

hash_digest block_download_coordinator::get_block_hash(uint32_t height) const {
    // Get hash from header organizer's index
    return organizer_.index().get_hash(static_cast<blockchain::header_index::index_t>(height));
}

void block_download_coordinator::flush_pending_to_validation() {
    // Push as many consecutive blocks as possible to validation channel
    while (!pending_blocks_.empty()) {
        auto it = pending_blocks_.find(next_height_to_validate_);
        if (it == pending_blocks_.end()) {
            // Next block not yet received
            break;
        }

        // Create the pair to send WITHOUT moving from pending_blocks yet
        auto block_pair = std::make_pair(next_height_to_validate_, it->second);

        // Try to send to validation channel
        bool sent = validation_queue_->try_send(std::error_code{}, std::move(block_pair));

        if (!sent) {
            // Channel full - block is still safe in pending_blocks, just exit
            // Rate-limit this log: only log occasionally to avoid spam
            static auto last_log = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_log > std::chrono::seconds(5)) {
                spdlog::warn("[coordinator] Validation channel full at height {} ({} pending)",
                    next_height_to_validate_, pending_blocks_.size());
                last_log = now;
            }
            break;
        }

        // Success - now remove from pending_blocks
        pending_blocks_.erase(it);
        ++next_height_to_validate_;
    }
}

void block_download_coordinator::set_failed(code reason) {
    if (failed_.exchange(true)) {
        return;  // Already failed
    }

    failure_reason_ = reason;
    spdlog::error("[coordinator] Sync failed: {}", reason.message());

    if (validation_queue_) {
        validation_queue_->close();
    }
}

} // namespace kth::node
