// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_DISPATCH_HPP
#define KTH_NODE_RPC_DISPATCH_HPP

#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/experimental/channel_select.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/mining/work_changes.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node {
class p2p_node;
} // namespace kth::node

namespace kth::node::mining {
struct job_store;
} // namespace kth::node::mining

namespace kth::node::rpc {

// How long a long-polled getblocktemplate is held before a mempool change
// alone answers it, BIP22's "after some time". Transactions arrive all the
// time, so a pool answered on each one would be polling again. A new tip
// answers at once.
inline constexpr std::chrono::seconds longpoll_mempool_wait{60};

using mining::doorbell;
using mining::ring_on_work_changes;
using mining::work_bell;

// Shared state a handler may reach: the chain, the getblocktemplatelight job
// cache, the P2P node and when the node started. Passed by reference; owned by
// the server for the connection's lifetime.
struct method_context {
    blockchain::block_chain& chain;
    mining::job_store& jobs;
    // Null when the server runs without one (tests); the network methods then
    // report it as unavailable.
    p2p_node const* network = nullptr;
    // When the node started, for uptime. The server passes the node's own
    // start; a context built without one (tests) counts from its creation.
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    // What wakes a request that waits (wait_for_work). Both are the server's;
    // a context built without one (tests) has neither, and such a request is
    // woken by its deadline alone.
    work_bell* work = nullptr;
    // Closed when the request's connection is gone: its client closed it, or
    // the server is stopping. Rung when the server can no longer watch the
    // connection, because the client sent more behind this request than the
    // server reads ahead. A request that outlived its connection would hold
    // one of the server's connections for an answer nobody reads.
    doorbell* closed = nullptr;
    // The long-polled getblocktemplate's minimum wait for a mempool change.
    // Tests shorten it.
    std::chrono::steady_clock::duration longpoll_wait = longpoll_mempool_wait;
};

// Why a request stopped waiting. connection_closed is a connection that is
// gone, or one the server can no longer watch.
enum class wake {
    work_changed,
    deadline,
    connection_closed,
};

// The doorbell the next change of the mining work closes, null without a
// server. Taken before reading the state the wait depends on.
[[nodiscard]]
std::shared_ptr<doorbell> watch_work(method_context const& ctx);

// Suspend the request until the mining work changes after `watched` was taken,
// `deadline` passes or its connection is gone or can no longer be watched,
// whichever is first. The connection wins over the others: nothing is left to
// answer, or more requests wait behind this one. The caller reads
// the chain again on work_changed; the wake says that something moved, not
// what. steady_clock::time_point::max() is no deadline.
[[nodiscard]]
::asio::awaitable<wake> wait_for_work(method_context const& ctx,
    std::shared_ptr<doorbell> watched, std::chrono::steady_clock::time_point deadline);

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
    // A batch (a JSON array of requests) is answered with an array holding each
    // member's response, in order; a member that is not an object gets an
    // invalid request error, and an empty batch an empty array.
    [[nodiscard]] ::asio::awaitable<std::string>
    handle(method_context& ctx, std::string_view body) const;

private:
    // One request object: its handler's outcome in a response envelope.
    [[nodiscard]] ::asio::awaitable<std::string>
    handle_request(method_context& ctx, std::string_view body) const;

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
