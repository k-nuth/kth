// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_DISPATCH_HPP
#define KTH_NODE_RPC_DISPATCH_HPP

#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include <asio/awaitable.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/json.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::rpc {

// A method handler serializes the JSON-RPC "result" fragment for `req`, or
// returns an rpc_error. It runs on the RPC coroutine and may co_await the chain.
using handler_fn = std::function<
    ::asio::awaitable<std::expected<std::string, rpc_error>>(
        blockchain::block_chain&, request const&)>;

// Routes a parsed JSON-RPC request to its registered handler and wraps the
// outcome (or any error) in a JSON-RPC response envelope.
struct dispatcher {
    void add(std::string method, handler_fn handler);
    [[nodiscard]] bool has(std::string_view method) const;

    // Parse `body`, invoke the matching handler, and return the full response body.
    [[nodiscard]] ::asio::awaitable<std::string>
    handle(blockchain::block_chain& chain, std::string_view body) const;

private:
    boost::unordered_flat_map<std::string, handler_fn> methods_;
};

// Register the built-in methods on `d` (getblockcount, ...).
void register_builtin_methods(dispatcher& d);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_DISPATCH_HPP
