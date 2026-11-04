// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_DISPATCH_HPP
#define KTH_NODE_RPC_DISPATCH_HPP

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <asio/awaitable.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/json.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::mining {
struct job_store;
} // namespace kth::node::mining

namespace kth::node::rpc {

// Shared state a handler may reach: the chain and the getblocktemplatelight job
// cache. Passed by reference; owned by the server for the connection's lifetime.
struct method_context {
    blockchain::block_chain& chain;
    mining::job_store& jobs;
};

// A method handler serializes the JSON-RPC "result" fragment for `req`, or
// returns an rpc_error. It runs on the RPC coroutine and may co_await the chain.
using handler_fn = std::function<
    ::asio::awaitable<std::expected<std::string, rpc_error>>(
        method_context&, request const&)>;

// Routes a parsed JSON-RPC request to its registered handler and wraps the
// outcome (or any error) in a JSON-RPC response envelope.
struct dispatcher {
    dispatcher() = default;

    // A handler may refer to the dispatcher it is registered on (help lists its
    // methods), so a dispatcher stays where it was built.
    dispatcher(dispatcher const&) = delete;
    dispatcher& operator=(dispatcher const&) = delete;

    // Complete registration before concurrent handle() calls begin. Install
    // help last: its results capture the complete method catalog.
    void add(std::string method, handler_fn handler);
    [[nodiscard]] bool has(std::string_view method) const;

    // The registered method names, sorted.
    [[nodiscard]] std::vector<std::string> const& methods() const noexcept;

    // Parse `body`, invoke the matching handler, and return the full response body.
    [[nodiscard]] ::asio::awaitable<std::string>
    handle(method_context& ctx, std::string_view body) const;

private:
    struct method_hash {
        using is_transparent = void;
        size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };
    boost::unordered_flat_map<std::string, handler_fn, method_hash, std::equal_to<>> methods_;
    std::vector<std::string> method_names_;
};

// Register custom methods first, then the built-in methods on `d`. Help is
// installed last and captures the completed method catalog.
void register_builtin_methods(dispatcher& d);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_DISPATCH_HPP
