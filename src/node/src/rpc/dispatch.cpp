// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/dispatch.hpp>

#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/node/rpc/error.hpp>

namespace kth::node::rpc {

void dispatcher::add(std::string method, handler_fn handler) {
    methods_.insert_or_assign(std::move(method), std::move(handler));
}

bool dispatcher::has(std::string_view method) const {
    return methods_.find(std::string(method)) != methods_.end();
}

::asio::awaitable<std::string>
dispatcher::handle(blockchain::block_chain& chain, std::string_view body) const {
    auto const req = parse_request(body);
    if ( ! req) {
        co_return build_error("null", req.error().code, req.error().message);
    }

    auto const it = methods_.find(req->method);
    if (it == methods_.end()) {
        co_return build_error(req->id, static_cast<int>(error_code::method_not_found),
            "Method not found");
    }

    auto const result = co_await it->second(chain, *req);
    if ( ! result) {
        co_return build_error(req->id, result.error().code, result.error().message);
    }
    co_return build_success(req->id, *result);
}

} // namespace kth::node::rpc
