// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/mining/work_changes.hpp>

#include <stop_token>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/mempool.hpp>

namespace kth::node::mining {

doorbell& work_changes::subscribe() {
    return subscribers_.emplace_back(1);
}

::asio::awaitable<void> work_changes::run(blockchain::block_chain& chain) {
    using namespace ::asio::experimental;
    auto const executor = co_await ::asio::this_coro::executor;
    // In this order: a stop ends it even when a change is pending too.
    channel_select_options options;
    options.biased = true;
    for (;;) {
        auto completion = co_await make_select(
            receive(ends_),
            receive(chain.tip_changes()),
            receive(chain.mempool_ref().changes())
        ).async_wait(executor, options, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        if (ec || selected.index == 0) {
            co_return;
        }
        for (auto& subscriber : subscribers_) {
            // Full means a ring it has not read yet, which covers this one.
            (void)subscriber.try_send(std::monostate{});
        }
    }
}

void work_changes::stop() {
    ends_.close();
}

std::shared_ptr<doorbell> work_bell::watch() {
    watched_ = true;
    return current_;
}

void work_bell::ring() {
    if ( ! watched_) {
        return;
    }
    watched_ = false;
    auto const rung = std::exchange(current_, std::make_shared<doorbell>(1));
    rung->close();
}

::asio::awaitable<void> ring_on_work_changes(doorbell& changes, work_bell& bell,
    doorbell& ends) {

    using namespace ::asio::experimental;
    auto const executor = co_await ::asio::this_coro::executor;
    // In this order: a stop ends it even when a change is pending too.
    channel_select_options options;
    options.biased = true;
    for (;;) {
        auto completion = co_await make_select(
            receive(ends),
            receive(changes)
        ).async_wait(executor, options, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        if (ec || selected.index == 0) {
            co_return;
        }
        bell.ring();
    }
}

} // namespace kth::node::mining
