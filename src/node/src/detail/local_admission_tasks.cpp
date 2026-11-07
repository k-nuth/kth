// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/local_admission_tasks.hpp>

#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/channel_select.hpp>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth::node::detail {

::asio::awaitable<void> local_body_handoff(
    sync::fast_validation_input_channel& fast_validation,
    sync::downloaded_chunk chunk,
    std::stop_token stop,
    sync::sync_coordinator_event_channel& events) {

    KTH_CONTRACT(chunk.local_result != nullptr);
    auto const submission = chunk.submission;
    auto const height = chunk.start_height;
    auto const slot = chunk.local_result;

    auto executor = co_await ::asio::this_coro::executor;

    // Into fast validation, waiting for room as a download worker does. A
    // stop before or during the wait ends this with the body never admitted;
    // the coordinator's exit answers the submission.
    {
        auto completion = co_await ::asio::experimental::make_select(
            ::asio::experimental::send(fast_validation, sync::fast_validation_input{std::move(chunk)})
        ).async_wait(executor, {}, stop, ::asio::as_tuple(::asio::deferred));
        auto& [ec, selected] = completion;
        (void)selected;
        if (ec) {
            spdlog::debug("[local_admission] Body of submission {} at height {} not handed over: {}",
                submission, height, ec.message());
            co_return;
        }
    }

    // The one terminal outcome, from whichever stage reached it.
    auto completion = co_await ::asio::experimental::make_select(
        ::asio::experimental::receive(*slot)
    ).async_wait(executor, {}, stop, ::asio::as_tuple(::asio::deferred));
    auto& [ec, selected] = completion;
    if (ec) {
        spdlog::debug("[local_admission] Outcome of submission {} at height {} not awaited: {}",
            submission, height, ec.message());
        co_return;
    }
    auto outcome = std::move(*selected.value<0>());

    // Carried with a wait, since the coordinator owes this submission an
    // answer and nothing else will bring it. A closed channel is the exit,
    // and the exit answers.
    auto [send_ec] = co_await events.async_send(std::error_code{}, std::move(outcome),
        ::asio::as_tuple(::asio::use_awaitable));
    if (send_ec) {
        spdlog::debug("[local_admission] Coordinator gone; outcome of submission {} dropped with it",
            submission);
    }
}

} // namespace kth::node::detail
