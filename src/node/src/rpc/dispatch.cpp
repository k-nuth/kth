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
    if ( ! is_batch(body)) {
        co_return co_await handle_request(ctx, body);
    }
    // A batch, as BCHN serves one: every member runs, in order, and each gets
    // the response it would get alone, errors included, with its id whenever
    // that can be read. Clients such as Rostrum match the responses to their
    // requests by id.
    auto const members = parse_batch(body);
    if ( ! members) {
        co_return build_error("null", members.error().code, members.error().message);
    }
    std::string reply = "[";
    bool first = true;
    for (auto const& member : *members) {
        if ( ! first) {
            reply += ',';
        }
        first = false;
        if ( ! member.object) {
            reply += build_error("null", static_cast<int>(error_code::invalid_request),
                "Invalid Request object");
            continue;
        }
        reply += co_await handle_request(ctx, member.raw);
    }
    reply += ']';
    co_return reply;
}

::asio::awaitable<std::string>
dispatcher::handle_request(method_context& ctx, std::string_view body) const {
    auto const req = parse_request(body);
    if ( ! req) {
        co_return build_error(req.error().id, req.error().error.code, req.error().error.message);
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
