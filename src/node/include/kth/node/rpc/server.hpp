// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_SERVER_HPP
#define KTH_NODE_RPC_SERVER_HPP

#include <string>
#include <string_view>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/settings.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::rpc {

// A minimal JSON-RPC-over-HTTP/1.1 server. It runs on the node's existing
// standalone-asio executor (no second runtime), parses requests with llhttp,
// and routes them through the dispatcher. Started only when rpc.enabled is true
// and the build defines KTH_WITH_RPC.
struct server {
    server(rpc_settings const& settings, blockchain::block_chain& chain);

    // Bind the listener and accept connections until the executor is stopped.
    // Intended to be co_spawned. Returns on a fatal bind error or shutdown.
    ::asio::awaitable<void> run(::asio::any_io_executor executor);

private:
    ::asio::awaitable<void> serve_connection(::asio::ip::tcp::socket socket);
    [[nodiscard]] bool authorized(std::string_view authorization_header) const;

    rpc_settings const& settings_;
    blockchain::block_chain& chain_;
    dispatcher dispatcher_;
    // Plaintext "user:password" (or "__cookie__:token") the Authorization header
    // must decode to.
    std::string expected_credentials_;
};

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_SERVER_HPP
