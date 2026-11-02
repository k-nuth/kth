// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/platform_timer.hpp>
#include <kth/node/detail/header_walk_tasks.hpp>

#include <algorithm>
#include <utility>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include <kth/network/protocols_coro.hpp>

namespace kth::node::detail {

// =============================================================================
// The ingest
// =============================================================================

::asio::awaitable<void> header_ingest_task(
    header_ingest_channels& channels,
    ingest_handoff_channel& handoff)
{
    spdlog::debug("[header_ingest] Task started");
    header_ingest queues;
    bool pulled = false;

    while (true) {
        auto [ec, command] = co_await channels.commands.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            spdlog::debug("[header_ingest] Commands closed");
            break;
        }

        if (auto* submit = std::get_if<ingest_submit>(&command)) {
            if ( ! queues.offer(submit->peer, std::move(submit->headers))) {
                // The peer's queue is full. Refused, not scored, and owed.
                spdlog::debug("[header_ingest] Batch from [{}] refused: its queue is full; "
                    "a follow-up is owed", submit->peer->authority());
                channels.owe();
            }
        } else {
            pulled = true;
        }

        if (pulled) {
            if (auto batch = queues.take()) {
                // One pull outstanding at most, and the handoff holds one: there
                // is room. Refused only when it is closed.
                if ( ! handoff.try_send(std::error_code{}, std::move(*batch))) {
                    break;
                }
                pulled = false;
            }
        }
    }

    handoff.cancel();
    handoff.close();
    spdlog::info("[header_ingest] Task ended");
}

::asio::awaitable<void> header_ingest_forwarder(
    header_ingest_channels& channels,
    ingest_handoff_channel& handoff,
    sync::header_validation_input_channel& validation)
{
    while (true) {
        auto [pull_ec] = co_await channels.commands.async_send(std::error_code{},
            ingest_pull{}, ::asio::as_tuple(::asio::use_awaitable));
        if (pull_ec) break;

        auto [take_ec, batch] = co_await handoff.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (take_ec) break;

        auto [send_ec] = co_await validation.async_send(std::error_code{},
            sync::downloaded_headers{
                .headers = std::move(batch.headers),
                .source_peer = std::move(batch.peer)},
            ::asio::as_tuple(::asio::use_awaitable));
        if (send_ec) break;
    }
    spdlog::debug("[header_ingest] Forwarder ended");
}

::asio::awaitable<void> header_debt_bridge(
    header_ingest_channels& channels,
    sync::sync_coordinator_event_channel& events)
{
    while (true) {
        auto [ec, ring] = co_await channels.debt.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if (ec) break;
        auto [send_ec] = co_await events.async_send(std::error_code{},
            sync::headers_owed_event{}, ::asio::as_tuple(::asio::use_awaitable));
        if (send_ec) break;
    }
    spdlog::debug("[header_ingest] Debt bridge ended");
}

// =============================================================================
// walk_timer
// =============================================================================

// Touched on the strand only, once created.
struct walk_timer::arming {
    explicit arming(::asio::strand<::asio::any_io_executor> const& strand)
        : timer(strand)
    {}

    ::kth::steady_timer timer;
    bool cancelled{false};
};

walk_timer::walk_timer(::asio::any_io_executor const& executor,
                       sync::sync_coordinator_event_channel& events,
                       spawner spawn)
    : strand_(::asio::make_strand(executor))
    , events_(events)
    , spawn_(std::move(spawn))
{}

void walk_timer::arm(std::chrono::milliseconds delay, sync::sync_coordinator_event event) {
    cancel();
    current_ = std::make_shared<arming>(strand_);

    auto waiting = [](std::shared_ptr<arming> state,
                      std::chrono::milliseconds delay,
                      sync::sync_coordinator_event event,
                      sync::sync_coordinator_event_channel& events) -> ::asio::awaitable<void> {
        // On the strand from here on.
        if (state->cancelled) {
            co_return;                  // cancelled before this task started
        }
        state->timer.expires_after(delay);
        auto [ec] = co_await state->timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if (ec || state->cancelled) {
            co_return;                  // cancelled while waiting, or as it woke
        }
        auto [send_ec] = co_await events.async_send(std::error_code{}, std::move(event),
            ::asio::as_tuple(::asio::use_awaitable));
        if (send_ec) {
            spdlog::debug("[header_walk] Coordinator gone; timed event dropped");
        }
    };

    spawn_(::asio::co_spawn(strand_,
        waiting(current_, delay, std::move(event), events_),
        ::asio::use_awaitable));
}

void walk_timer::cancel() {
    if ( ! current_) {
        return;
    }
    ::asio::post(strand_, [state = std::move(current_)] {
        state->cancelled = true;
        state->timer.cancel();
    });
    current_.reset();
}

// =============================================================================
// stimulus_sender
// =============================================================================

// Touched on the strand only, once created.
struct stimulus_sender::sending {
    bool cancelled{false};
    ::asio::cancellation_signal signal;
};

stimulus_sender::stimulus_sender(::asio::any_io_executor const& executor,
                                 sync::sync_coordinator_event_channel& events,
                                 spawner spawn)
    : strand_(::asio::make_strand(executor))
    , events_(events)
    , spawn_(std::move(spawn))
{}

void stimulus_sender::send(network::peer_session::ptr peer, hash_list locator, uint64_t seq) {
    cancel();
    current_ = std::make_shared<sending>();

    auto sending_task = [](std::shared_ptr<sending> state,
                           ::asio::strand<::asio::any_io_executor> strand,
                           network::peer_session::ptr peer,
                           hash_list locator,
                           uint64_t seq,
                           sync::sync_coordinator_event_channel& events) -> ::asio::awaitable<void> {
        // On the strand from here on.
        if (state->cancelled) {
            co_return;                  // withdrawn before this task started
        }

        // The send itself, bound to this stimulus's cancellation slot. Bound
        // here, on the strand, where the slot is also emitted.
        auto const ec = co_await ::asio::co_spawn(strand,
            network::send_get_headers(*peer, locator, null_hash),
            ::asio::bind_cancellation_slot(state->signal.slot(), ::asio::use_awaitable));

        if (ec == error::success) {
            spdlog::debug("[header_walk] Stimulus {} accepted for {}", seq, peer->authority_with_agent());
            auto [send_ec] = co_await events.async_send(std::error_code{},
                sync::header_stimulus_accepted{.seq = seq}, ::asio::as_tuple(::asio::use_awaitable));
            (void)send_ec;
            co_return;
        }
        if (state->cancelled) {
            co_return;                  // withdrawn while it waited for room
        }
        spdlog::debug("[header_walk] Stimulus {} to {} failed: {}",
            seq, peer->authority_with_agent(), ec.message());
        auto [send_ec] = co_await events.async_send(std::error_code{},
            sync::header_stimulus_failed{.seq = seq}, ::asio::as_tuple(::asio::use_awaitable));
        (void)send_ec;
    };

    spawn_(::asio::co_spawn(strand_,
        sending_task(current_, strand_, std::move(peer), std::move(locator), seq, events_),
        ::asio::use_awaitable));
}

void stimulus_sender::cancel() {
    if ( ! current_) {
        return;
    }
    ::asio::post(strand_, [state = std::move(current_)] {
        state->cancelled = true;
        state->signal.emit(::asio::cancellation_type::terminal);
    });
    current_.reset();
}

// =============================================================================

std::chrono::milliseconds walk_retry_delay(std::chrono::milliseconds base,
                                           std::chrono::milliseconds cap,
                                           uint32_t attempt) {
    auto delay = std::min(base, cap);
    for (uint32_t i = 0; i < attempt && delay < cap; ++i) {
        delay = std::min(delay * 2, cap);
    }
    return delay;
}

} // namespace kth::node::detail
