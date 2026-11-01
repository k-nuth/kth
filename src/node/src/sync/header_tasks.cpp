// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_tasks.hpp>

#include <spdlog/spdlog.h>

#include <asio/as_tuple.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::node::sync {

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
            spdlog::debug("[header_validation] Validating {} headers from {}",
                downloaded->headers.size(),
                downloaded->source_peer ? downloaded->source_peer->authority_with_agent() : "unknown");

            // Single writer to organizer - no lock needed
            auto result = organizer.add_headers(downloaded->headers);

            spdlog::debug("[header_validation] Added {} headers, total index size: {}",
                result.headers_added, result.index_size);

            // Awaited. This result is all the coordinator ever learns about the
            // batch: dropped, a stimulus it answers would wait out its deadline
            // for nothing, and a batch that moved the tip would move nothing
            // else.
            auto [send_ec] = co_await output.async_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = result.headers_added,
                .result = result.error,
                .source_peer = downloaded->source_peer,
                .reorg_candidate = result.reorg_candidate,
                .reorg_fork_height = result.reorg_fork_height,
                .reorg_branch_head = result.reorg_branch_head
            }, ::asio::as_tuple(::asio::use_awaitable));

            if (send_ec) {
                spdlog::debug("[header_validation] Output channel closed");
                break;
            }
        }

        if (auto* submitted = std::get_if<block_submission>(&msg)) {
            spdlog::debug("[header_validation] Validating the header of submitted block {} at {}",
                encode_hash(submitted->block->hash()), submitted->height);

            // The same single writer, for a header that came with its block.
            auto result = organizer.add_headers(
                domain::message::header::list{domain::message::header(submitted->block->header())});

            auto [send_ec] = co_await output.async_send(std::error_code{}, headers_validated{
                .height = uint32_t(organizer.header_height()),
                .count = result.headers_added,
                .result = result.error,
                .source_peer = nullptr,
                .reorg_candidate = result.reorg_candidate,
                .reorg_fork_height = result.reorg_fork_height,
                .reorg_branch_head = result.reorg_branch_head,
                .submission = *submitted
            }, ::asio::as_tuple(::asio::use_awaitable));

            if (send_ec) {
                (*submitted->answer)(error::service_stopped);
                spdlog::debug("[header_validation] Output channel closed");
                break;
            }
        }
    }

    // NOTE: Don't close output channel here - peer_provider closes all channels during shutdown
    spdlog::info("[header_validation] Task ended");
}

} // namespace kth::node::sync
