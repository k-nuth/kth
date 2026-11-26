// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_SERVER_HPP
#define KTH_NODE_RPC_SERVER_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/strand.hpp>
#include <asio/experimental/concurrent_channel.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/mining/job_store.hpp>
#include <kth/node/settings.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node {
class p2p_node;
} // namespace kth::node

namespace kth::node::rpc {

// A minimal JSON-RPC-over-HTTP/1.1 server. It runs on the node's existing
// standalone-asio executor (no second runtime), parses requests with llhttp,
// and routes them through the dispatcher. Started only when rpc.enabled is true
// and the build defines KTH_WITH_RPC.
struct server {
    // `started` is when the node started, which uptime counts from. It
    // defaults to the server's construction for servers built on their own.
    // `methods` registers further methods before the built-in ones.
    // `work_changes` is this server's subscription to the node's
    // mining::work_changes: each ring wakes the requests held on mining work.
    // Without one (a server built on its own) they are woken by their
    // deadline and their connection.
    server(rpc_settings const& settings, blockchain::block_chain& chain,
        ::asio::any_io_executor executor, p2p_node const* network = nullptr,
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now(),
        std::function<void(dispatcher&)> const& methods = {},
        doorbell* work_changes = nullptr);

    // Bind the listener and accept connections until the executor is stopped.
    // Intended to be co_spawned. Returns on a fatal bind error or shutdown.
    ::asio::awaitable<void> run();

    // A bounded stop message, including when run() has not started yet.
    // Socket cancellation itself is performed only by the RPC owner strand.
    // A request held on mining work ends with its connection.
    void stop();

    // One startup outcome: the actual bound port, or the listener's error.
    // May be awaited before run() starts. Cancellation wakes the waiter.
    [[nodiscard]] ::asio::awaitable<uint16_t> wait_until_listening();

private:
    ::asio::awaitable<void> run_owned();
    // `closed` is the connection's: the read beside a handler closes it when
    // the client is gone, and the stop closes it for every connection.
    ::asio::awaitable<void> serve_connection(::asio::ip::tcp::socket& socket, doorbell& closed);
    [[nodiscard]] bool authorized(std::string_view authorization_header) const;

    rpc_settings const& settings_;
    blockchain::block_chain& chain_;
    p2p_node const* network_;
    std::chrono::steady_clock::time_point const started_;
    dispatcher dispatcher_;
    mining::job_store jobs_;
    // Plaintext "user:password" (or "__cookie__:token") the Authorization header
    // must decode to.
    std::string expected_credentials_;
    ::asio::strand<::asio::any_io_executor> owner_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> stop_requests_;
    ::asio::experimental::concurrent_channel<void(std::error_code, uint16_t)> listening_;
    // Wakes the requests held on mining work. Rung by run_owned() on each
    // ring of work_changes_; owner_ serializes every use.
    work_bell work_;
    doorbell* const work_changes_;
};

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_SERVER_HPP
