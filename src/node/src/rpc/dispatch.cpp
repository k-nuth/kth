// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/dispatch.hpp>

#include <algorithm>
#include <stop_token>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/deferred.hpp>
#include <asio/this_coro.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/mempool.hpp>

#include <kth/node/rpc/error.hpp>

namespace kth::node::rpc {

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

::asio::awaitable<void> ring_on_chain_changes(blockchain::block_chain& chain,
    work_bell& bell, doorbell& ends) {

    using namespace ::asio::experimental;
    auto const executor = co_await ::asio::this_coro::executor;
    for (;;) {
        auto completion = co_await make_select(
            receive(ends),
            receive(chain.tip_changes()),
            receive(chain.mempool_ref().changes())
        ).async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto const& [ec, selected] = completion;
        if (ec || selected.index == 0) {
            co_return;
        }
        bell.ring();
    }
}

std::shared_ptr<doorbell> watch_work(method_context const& ctx) {
    return ctx.work != nullptr ? ctx.work->watch() : nullptr;
}

::asio::awaitable<wake> wait_for_work(method_context const& ctx,
    std::shared_ptr<doorbell> watched, std::chrono::steady_clock::time_point deadline) {

    using namespace ::asio::experimental;
    auto const executor = co_await ::asio::this_coro::executor;
    // Stands in for a doorbell this request does not have; nothing rings it.
    doorbell silent{1};
    // In this order: a connection that is gone ends the wait even when the
    // work changed too.
    channel_select_options options;
    options.biased = true;
    auto completion = co_await make_select(
        receive(ctx.closed != nullptr ? *ctx.closed : silent),
        receive(watched ? *watched : silent),
        after(deadline)
    ).async_wait(executor, options, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
    auto const& [ec, selected] = completion;
    if (selected.index == 1) {
        co_return wake::work_changed;
    }
    if (selected.index == 2) {
        co_return wake::deadline;
    }
    // The connection, or a wait that could not be made: neither is a reason
    // to keep the request held.
    co_return wake::connection_closed;
}

void dispatcher::add(std::string method, handler_fn handler) {
    if (auto const found = methods_.find(method); found != methods_.end()) {
        found->second = std::move(handler);
        return;
    }

    // Prepare the sorted names before changing the table, so an allocation
    // failure leaves the two in agreement.
    auto names = method_names_;
    names.insert(std::ranges::lower_bound(names, method), method);
    methods_.emplace(std::move(method), std::move(handler));
    method_names_.swap(names);
}

bool dispatcher::has(std::string_view method) const {
    return methods_.find(method) != methods_.end();
}

std::vector<std::string> const& dispatcher::methods() const noexcept {
    return method_names_;
}

::asio::awaitable<std::string>
dispatcher::handle(method_context& ctx, std::string_view body) const {
    auto const req = parse_request(body);
    if ( ! req) {
        co_return build_error("null", req.error().code, req.error().message);
    }

    auto const it = methods_.find(req->method);
    if (it == methods_.end()) {
        co_return build_error(req->id, static_cast<int>(error_code::method_not_found),
            "Method not found");
    }

    auto const result = co_await it->second(ctx, *req);
    if ( ! result) {
        co_return build_error(req->id, result.error().code, result.error().message);
    }
    co_return build_success(req->id, *result);
}

} // namespace kth::node::rpc
