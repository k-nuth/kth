// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_tasks.hpp>

#include <chrono>
#include <optional>
#include <vector>

#include <boost/unordered/unordered_flat_set.hpp>
#include <spdlog/spdlog.h>

#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <kth/network/protocols_coro.hpp>

namespace kth::node::sync {

using namespace ::asio::experimental::awaitable_operators;

// =============================================================================
// Header Download Task
// =============================================================================

::asio::awaitable<void> header_download_task(
    header_download_input_channel& input,
    header_download_output_channel& output
) {
    spdlog::debug("[header_download] Task started");

    // Local copy of peers - updated by peer_provider (already filtered)
    std::vector<network::peer_session::ptr> available_peers;
    std::optional<header_request> pending_request;  // Buffered request waiting for peers

    // Sticky peer selection - use same peer until failure
    network::peer_session::ptr current_peer;

    // Track last failed peer nonce - don't reselect until we get peers_updated
    std::optional<uint64_t> last_failed_nonce;

    // Track peers that returned empty (at their tip) - don't reselect for headers
    boost::unordered_flat_set<uint64_t> peers_at_tip;

    // Track best known height from peer VERSION messages
    // This tells us the highest height any peer claims to be at
    uint32_t best_known_height = 0;

    // Helper to compute best known height from available peers
    auto update_best_known_height = [&]() {
        best_known_height = 0;
        for (auto const& p : available_peers) {
            if (p->stopped()) continue;
            if (auto pv = p->peer_version(); pv) {
                best_known_height = std::max(best_known_height, pv->start_height());
            }
        }
    };

    // Helper to get peer for headers (sticky - same peer until failure)
    auto get_header_peer = [&]() -> network::peer_session::ptr {
        // If current peer is still valid and in available list, use it
        if (current_peer && !current_peer->stopped()) {
            // Don't use if it just failed (waiting for peers_updated)
            if (last_failed_nonce && current_peer->nonce() == *last_failed_nonce) {
                current_peer = nullptr;
            } else {
                // Check if still in available_peers (peer_provider may have removed it)
                for (auto const& p : available_peers) {
                    if (p->nonce() == current_peer->nonce()) {
                        return current_peer;
                    }
                }
                // Current peer was removed by peer_provider, clear it
                current_peer = nullptr;
            }
        }

        // Need to select a new peer - skip stopped peers, the last failed one, and peers at their tip
        // NOTE: Don't modify available_peers here - just skip invalid ones
        for (auto const& p : available_peers) {
            if (p->stopped()) continue;  // Skip stopped peers without erasing
            if (last_failed_nonce && p->nonce() == *last_failed_nonce) continue;  // Skip failed peer
            if (peers_at_tip.contains(p->nonce())) continue;  // Skip peers that returned empty
            current_peer = p;
            spdlog::info("[header_sync] Using peer: {}", current_peer->authority_with_agent());
            return current_peer;
        }

        // All peers are either stopped, failed, or at their tip - wait for peers_updated
        return nullptr;
    };

    // Helper lambda to process a request
    auto process_request = [&](header_request const& request) -> ::asio::awaitable<void> {
        auto peer = get_header_peer();
        if (!peer) {
            spdlog::debug("[header_download] No peers available, buffering request for height {}",
                request.from_height);
            pending_request = request;
            co_return;
        }

        spdlog::debug("[header_download] Requesting headers from {} at height {}",
            peer->authority_with_agent(), request.from_height);

        // Download headers with timing
        auto start_time = std::chrono::steady_clock::now();
        // 2026-02-02: Reduced from 30s to 10s to minimize stalls from slow peers
        auto result = co_await network::request_headers_from(
            *peer, request.from_hash, std::chrono::seconds(10));
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        if (!result) {
            // 2026-01-28: Added detailed logging to diagnose segfault during shutdown.
            // Symptom: After Ctrl-C, we see "Peer X failed: channel stopped" then immediate segfault.
            // Adding checkpoints to identify exactly which operation crashes.
            spdlog::warn("[header_sync] Peer {} failed: {}",
                peer->authority_with_agent(), result.error().message());
            spdlog::warn("[header_sync] checkpoint 1: after log");

            // Mark this peer as failed - don't reselect until peers_updated
            auto const peer_nonce = peer->nonce();
            spdlog::warn("[header_sync] checkpoint 2: got nonce {}", peer_nonce);
            last_failed_nonce = peer_nonce;
            current_peer = nullptr;
            spdlog::warn("[header_sync] checkpoint 3: cleared current_peer");

            // Report failure to output (coordinator will inform peer_provider)
            // Note: During shutdown, the channel may be closed. try_send returns false on closed channel.
            auto const error_to_report = result.error();
            spdlog::warn("[header_sync] checkpoint 4: got error code");
            bool send_ok = output.try_send(std::error_code{}, peer_failure_report{
                .peer = peer,
                .error = error_to_report
            });
            spdlog::warn("[header_sync] checkpoint 5: try_send returned {}", send_ok);
            if (!send_ok) {
                spdlog::warn("[header_download] Channel full or closed, peer failure report dropped");
            }
            // Keep request pending - will retry with new peer after peers_updated
            pending_request = request;
            spdlog::warn("[header_sync] checkpoint 6: returning from error handler");
            co_return;
        }

        if (result->elements().empty()) {
            // Peer has no more headers - mark as "at their tip" and try another peer
            auto peer_start_height = peer->peer_version() ? peer->peer_version()->start_height() : 0;
            spdlog::info("[header_download] Peer {} (start_height={}) returned 0 headers at height {}, trying another peer ({} at tip so far)",
                peer->authority_with_agent(), peer_start_height, request.from_height, peers_at_tip.size() + 1);

            peers_at_tip.insert(peer->nonce());
            current_peer = nullptr;  // Force selection of new peer

            // Check if there's another peer available
            auto next_peer = get_header_peer();
            if (!next_peer) {
                // All available peers have returned 0 headers
                // Check if we've reached the best known height from peer announcements
                if (request.from_height >= best_known_height) {
                    // We've reached or exceeded the best known tip - signal sync complete
                    spdlog::info("[header_download] All {} peers at their tip, signaling sync complete at height {} (best_known={})",
                        available_peers.size(), request.from_height, best_known_height);

                    if (!output.try_send(std::error_code{}, downloaded_headers{
                        .headers = {},
                        .start_height = request.from_height,
                        .source_peer = peer
                    })) {
                        spdlog::warn("[header_download] Channel full, empty headers signal dropped");
                    }
                    pending_request.reset();
                } else {
                    // We're below the best known height but all peers returned 0
                    // This is suspicious - peers claimed higher but can't provide headers
                    // Keep request pending and wait for peers_updated (new peers or peers sync more)
                    spdlog::warn("[header_download] All peers returned 0 but we're at {} < best_known={}, waiting for more peers",
                        request.from_height, best_known_height);
                    pending_request = request;
                }
                co_return;
            }

            // Another peer available - keep request pending for immediate retry
            // (The main loop will process pending_request before waiting for new events)
            pending_request = request;
            co_return;
        }

        auto headers_count = uint32_t(result->elements().size());
        spdlog::debug("[header_download] Received {} headers from {} in {}ms",
            headers_count, peer->authority_with_agent(), elapsed_ms);

        // Send to validation
        if (!output.try_send(std::error_code{}, downloaded_headers{
            .headers = result->elements(),
            .start_height = request.from_height + 1,
            .source_peer = peer
        })) {
            spdlog::warn("[header_download] Channel full, headers dropped");
            pending_request = request;
            co_return;
        }

        // Report performance to peer_provider (for slow peer tracking)
        if (!output.try_send(std::error_code{}, header_performance{
            .peer_nonce = peer->nonce(),
            .headers_downloaded = headers_count,
            .download_time_ms = uint32_t(elapsed_ms)
        })) {
            spdlog::debug("[header_download] Channel full, performance report dropped");
            // Non-fatal, continue
        }

        // Clear pending since we processed it
        pending_request.reset();
    };

    // Single channel, FIFO processing - no priority issues
    while (true) {
        spdlog::debug("[header_download] Waiting for events (peers={}, pending={})",
            available_peers.size(), pending_request.has_value());

        auto [ec, event] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_download] Input channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(event)) {
            spdlog::debug("[header_download] Stop signal received");
            break;
        }

        if (auto* peers_msg = std::get_if<peers_updated>(&event)) {
            available_peers = peers_msg->peers;

            // 2026-01-28: Fix for header sync getting stuck retrying the same failed peer.
            // Symptom: After a peer times out, the log showed:
            //   [header_sync] Peer X failed: connection timed out
            //   [header_sync] Using peer: X   <-- same peer immediately reselected!
            // This repeated every 30 seconds indefinitely, ignoring 7-8 other available peers.
            //
            // Root cause: Race condition between peers_updated messages and peer failures.
            // 1. peer_provider broadcasts peers (peer X in list)
            // 2. header_download waits 30s for peer X response
            // 3. Meanwhile, new peers connect -> peer_provider broadcasts again (X still in list)
            // 4. Timeout fires, X fails -> last_failed_nonce = X
            // 5. header_download processes queued peers_updated from step 3
            // 6. OLD CODE: last_failed_nonce.reset() unconditionally <- BUG! Clears protection
            // 7. header_download selects X again (still in the stale list)
            //
            // Fix: Only clear last_failed_nonce if the failed peer is NOT in the new list.
            // If the failed peer is still present, this is a stale message from before the
            // failure, so we keep the protection active.
            if (last_failed_nonce) {
                bool failed_peer_still_present = false;
                for (auto const& p : available_peers) {
                    if (p->nonce() == *last_failed_nonce) {
                        failed_peer_still_present = true;
                        break;
                    }
                }
                if (!failed_peer_still_present) {
                    spdlog::debug("[header_download] Failed peer {} no longer in list, clearing protection",
                        *last_failed_nonce);
                    last_failed_nonce.reset();
                } else {
                    spdlog::debug("[header_download] Failed peer {} still in list, keeping protection",
                        *last_failed_nonce);
                }
            }

            // Update best known height from all peers
            update_best_known_height();

            spdlog::debug("[header_download] Peers updated: {} peers available, best_known_height={}",
                available_peers.size(), best_known_height);

            // 2026-02-02: Fix infinite loop in header sync completion detection.
            //
            // Bug: Previously we cleared ALL peers_at_tip on every peers_updated message.
            // Since peers_updated fires frequently (peer connects/disconnects), we never
            // reached the "all peers at tip" condition needed to complete header sync.
            // The loop was:
            //   1. BCHN peers return 0 headers → marked as at_tip
            //   2. peers_updated arrives → clears peers_at_tip
            //   3. Retry same BCHN peers → return 0 → marked as at_tip
            //   4. Another peers_updated → ... infinite loop
            //
            // Fix: Only remove disconnected peers from peers_at_tip. Keep connected peers
            // that already said they're at tip - they probably still are.
            // Now when all available peers are in peers_at_tip, we correctly detect
            // that header sync is complete and trigger block sync.
            if (!peers_at_tip.empty()) {
                size_t const before = peers_at_tip.size();
                // Build set of current peer nonces for O(1) lookup
                boost::unordered_flat_set<uint64_t> current_nonces;
                for (auto const& p : available_peers) {
                    current_nonces.insert(p->nonce());
                }
                // Remove peers that are no longer connected
                erase_if(peers_at_tip, [&](uint64_t nonce) {
                    return !current_nonces.contains(nonce);
                });
                if (peers_at_tip.size() != before) {
                    spdlog::debug("[header_download] Removed {} disconnected peers from peers_at_tip (now {})",
                        before - peers_at_tip.size(), peers_at_tip.size());
                }
            }

            // If we have a pending request and now have peers, process it
            if (pending_request && !available_peers.empty()) {
                spdlog::debug("[header_download] Retrying pending request with {} peers",
                    available_peers.size());
                co_await process_request(*pending_request);
            }
            continue;
        }

        if (auto* request = std::get_if<header_request>(&event)) {
            spdlog::debug("[header_download] Request received: height={}", request->from_height);
            co_await process_request(*request);
            continue;
        }
    }

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[header_download] Task ended");
}

// =============================================================================
// Header Validation Task
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    header_validation_input_channel& input,
    header_validated_channel& output
) {
    spdlog::debug("[header_validation] Task started");

    // Single channel, FIFO processing - no priority issues
    while (true) {
        auto [ec, msg] = co_await input.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            spdlog::debug("[header_validation] Input channel closed");
            break;
        }

        // Process message based on variant type (FIFO order guaranteed)
        if (std::holds_alternative<stop_request>(msg)) {
            spdlog::debug("[header_validation] Stop signal received");
            break;
        }

        if (auto* downloaded = std::get_if<downloaded_headers>(&msg)) {
            spdlog::debug("[header_validation] Validating {} headers from height {}",
                downloaded->headers.size(), downloaded->start_height);

            // Single writer to organizer - no lock needed
            auto result = organizer.add_headers(downloaded->headers);

            spdlog::debug("[header_validation] Added {} headers, total index size: {}",
                result.headers_added, result.index_size);

            if (!output.try_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = result.headers_added,
                .result = result.error,
                .source_peer = downloaded->source_peer
            })) {
                spdlog::warn("[header_validation] Channel full, headers_validated dropped");
                break;
            }
        } else if (auto* failure = std::get_if<peer_failure_report>(&msg)) {
            // Peer failure - forward to coordinator so it can retry with another peer
            spdlog::debug("[header_validation] Received peer failure report for {}: {}",
                failure->peer->authority_with_agent(), failure->error.message());

            // Forward failure to coordinator - it will retry header sync
            if (!output.try_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = 0,
                .result = failure->error,
                .source_peer = failure->peer
            })) {
                spdlog::warn("[header_validation] Channel full, peer failure dropped");
            }
        }
    }

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[header_validation] Task ended");
}

} // namespace kth::node::sync
