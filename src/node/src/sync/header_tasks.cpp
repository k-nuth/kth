// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_tasks.hpp>

#include <optional>
#include <vector>

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

        // Need to select a new peer - skip the last failed one
        std::erase_if(available_peers, [](auto const& p) { return p->stopped(); });

        for (auto const& p : available_peers) {
            if (!last_failed_nonce || p->nonce() != *last_failed_nonce) {
                current_peer = p;
                spdlog::debug("[header_download] Selected peer: {}", current_peer->authority_with_agent());
                return current_peer;
            }
        }

        // All peers are either stopped or the failed one - wait for peers_updated
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

        // Download headers
        auto result = co_await network::request_headers_from(
            *peer, request.from_hash, std::chrono::seconds(30));

        if (!result) {
            spdlog::debug("[header_download] Failed to get headers from {}: {}",
                peer->authority_with_agent(), result.error().message());

            // Mark this peer as failed - don't reselect until peers_updated
            last_failed_nonce = peer->nonce();
            current_peer = nullptr;

            // Report failure to output (coordinator will inform peer_provider)
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                peer_failure_report{
                    .peer = peer,
                    .error = result.error()
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_download] Failed to send peer failure report: {}", send_ec.message());
            }
            // Keep request pending - will retry with new peer after peers_updated
            pending_request = request;
            co_return;
        }

        if (result->elements().empty()) {
            spdlog::debug("[header_download] No new headers from {} (at tip), triggering block sync",
                peer->authority_with_agent());
            // Send empty message to signal sync complete
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                downloaded_headers{
                    .headers = {},
                    .start_height = request.from_height,
                    .source_peer = peer
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_download] Failed to send empty headers: {}", send_ec.message());
            }
            pending_request.reset();
            co_return;
        }

        spdlog::debug("[header_download] Received {} headers from {}",
            result->elements().size(), peer->authority_with_agent());

        // Send to validation
        auto [send_ec] = co_await output.async_send(
            std::error_code{},
            downloaded_headers{
                .headers = result->elements(),
                .start_height = request.from_height + 1,
                .source_peer = peer
            },
            ::asio::as_tuple(::asio::use_awaitable)
        );
        if (send_ec) {
            spdlog::warn("[header_download] Failed to send headers to validation: {}", send_ec.message());
            pending_request = request;
            co_return;
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
            // Clear failed nonce - peer_provider has filtered the bad peer
            last_failed_nonce.reset();
            spdlog::debug("[header_download] Peers updated: {} peers available",
                available_peers.size());

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

    // Close output channel to signal validation task that no more headers are coming
    output.close();
    spdlog::debug("[header_download] Task ended");
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

            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                headers_validated{
                    .height = uint32_t(organizer.header_height()),
                    .count = result.headers_added,
                    .result = result.error,
                    .source_peer = downloaded->source_peer
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_validation] Failed to send validation result: {}", send_ec.message());
                break;
            }
        } else if (auto* failure = std::get_if<peer_failure_report>(&msg)) {
            // Peer failure - forward to coordinator so it can retry with another peer
            spdlog::debug("[header_validation] Received peer failure report for {}: {}",
                failure->peer->authority_with_agent(), failure->error.message());

            // Forward failure to coordinator - it will retry header sync
            auto [send_ec] = co_await output.async_send(
                std::error_code{},
                headers_validated{
                    .height = uint32_t(organizer.header_height()),
                    .count = 0,
                    .result = failure->error,
                    .source_peer = failure->peer
                },
                ::asio::as_tuple(::asio::use_awaitable)
            );
            if (send_ec) {
                spdlog::warn("[header_validation] Failed to forward peer failure: {}", send_ec.message());
            }
        }
    }

    // Close output channel to signal coordinator
    output.close();
    spdlog::debug("[header_validation] Task ended");
}

} // namespace kth::node::sync
