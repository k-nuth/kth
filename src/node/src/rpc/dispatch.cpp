// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/dispatch.hpp>

#include <algorithm>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/node/rpc/error.hpp>

namespace kth::node::rpc {

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
