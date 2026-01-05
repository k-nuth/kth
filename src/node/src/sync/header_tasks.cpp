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
    peer_channel& peers,
    header_request_channel& requests,
    header_download_channel& output,
    stop_channel& stop
) {
    spdlog::debug("[header_download] Task started");

    // Local copy of peers - updated whenever we receive peers_updated
    std::vector<network::peer_session::ptr> available_peers;
    std::optional<header_request> pending_request;  // Buffered request waiting for peers
    size_t current_peer_index = 0;  // Round-robin peer selection

    // Helper to get next peer (round-robin)
    auto get_next_peer = [&]() -> network::peer_session::ptr {
        // Clean stopped peers first
        std::erase_if(available_peers, [](auto const& p) { return p->stopped(); });
        if (available_peers.empty()) {
            return nullptr;
        }
        current_peer_index = current_peer_index % available_peers.size();
        return available_peers[current_peer_index++];
    };

    // Helper lambda to process a request
    auto process_request = [&](header_request const& request) -> ::asio::awaitable<void> {
        auto peer = get_next_peer();
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
            spdlog::warn("[header_download] Failed to get headers from {}: {}",
                peer->authority_with_agent(), result.error().message());
            // Report failure to coordinator (peer manager will handle it)
            output.try_send(std::error_code{}, peer_failure_report{
                .peer = peer,
                .error = result.error()
            });
            // Don't clear pending_request - coordinator will send updated peer list
            // and we'll retry with the next peer
            pending_request = request;
            co_return;
        }

        if (result->elements().empty()) {
            spdlog::debug("[header_download] No new headers from {} (at tip), triggering block sync",
                peer->authority_with_agent());
            // Send empty message to signal sync complete
            output.try_send(std::error_code{}, downloaded_headers{
                .headers = {},
                .start_height = request.from_height,
                .source_peer = peer
            });
            pending_request.reset();
            co_return;
        }

        spdlog::debug("[header_download] Received {} headers from {}",
            result->elements().size(), peer->authority_with_agent());

        // Send to validation
        output.try_send(std::error_code{}, downloaded_headers{
            .headers = result->elements(),
            .start_height = request.from_height + 1,
            .source_peer = peer
        });

        // Clear pending since we processed it
        pending_request.reset();
    };

    while (true) {
        // Wait for: stop (priority), peers update, or request
        auto event = co_await (
            stop.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            peers.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            requests.async_receive(::asio::as_tuple(::asio::use_awaitable))
        );

        if (event.index() == 0) {
            spdlog::debug("[header_download] Stop signal received");
            break;
        }

        if (event.index() == 1) {
            // Peers list updated
            auto [ec, peers_msg] = std::get<1>(event);
            if (ec) {
                spdlog::debug("[header_download] Peers channel closed");
                break;
            }

            available_peers = peers_msg.peers;
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

        // New request received (index 2)
        auto [ec, request] = std::get<2>(event);
        if (ec) {
            spdlog::debug("[header_download] Requests channel closed");
            break;
        }
        spdlog::debug("[header_download] Request received: height={}", request.from_height);

        co_await process_request(request);
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
    header_download_channel& input,
    header_validated_channel& output,
    stop_channel& stop
) {
    spdlog::debug("[header_validation] Task started");

    while (true) {
        // Stop signal first in || to prioritize shutdown over buffered data
        auto event = co_await (
            stop.async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            input.async_receive(::asio::as_tuple(::asio::use_awaitable))
        );

        if (event.index() == 0) {
            spdlog::debug("[header_validation] Stop signal received");
            break;
        }

        auto [ec, download_event] = std::get<1>(event);
        if (ec) {
            spdlog::debug("[header_validation] Input channel closed");
            break;
        }

        // Handle variant: only process downloaded_headers
        if (auto* downloaded = std::get_if<downloaded_headers>(&download_event)) {
            spdlog::debug("[header_validation] Validating {} headers from height {}",
                downloaded->headers.size(), downloaded->start_height);

            // Single writer to organizer - no lock needed
            auto result = organizer.add_headers(downloaded->headers);

            spdlog::debug("[header_validation] Added {} headers, total index size: {}",
                result.headers_added, result.index_size);

            bool sent = output.try_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = result.headers_added,
                .result = result.error,
                .source_peer = downloaded->source_peer
            });
            spdlog::debug("[header_validation] Sent to output channel: {}", sent ? "success" : "FAILED");
        } else if (auto* failure = std::get_if<peer_failure_report>(&download_event)) {
            // Peer failure - just log, coordinator should handle this
            spdlog::debug("[header_validation] Received peer failure report for {}: {}",
                failure->peer->authority_with_agent(), failure->error.message());
            // Could forward to coordinator via another channel if needed
        }
    }

    // Close output channel to signal coordinator
    output.close();
    spdlog::debug("[header_validation] Task ended");
}

} // namespace kth::node::sync
