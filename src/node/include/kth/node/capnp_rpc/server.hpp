// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_CAPNP_RPC_SERVER_HPP
#define KTH_NODE_CAPNP_RPC_SERVER_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/node/capnp_rpc/reader.hpp>
#include <kth/node/define.hpp>
#include <kth/node/settings.hpp>

namespace kth::node::capnp_rpc {

// Every bound is a hard one; nothing queues without limit.
struct limits {
    // Open client connections; further connects are closed at once.
    std::size_t max_connections = 16;
    // Calls admitted but not finished, across all connections. A call above the
    // bound is answered resourceExhausted without being queued.
    std::size_t max_in_flight = 16;
    // The same, per connection, so one client cannot take every slot.
    std::size_t max_in_flight_per_connection = 4;
    // Largest incoming RPC message. Requests are tiny; this bounds what an
    // unauthenticated peer can make the node buffer.
    std::size_t max_request_bytes = 16 * 1024;
    // Largest block/transaction payload. Larger results are answered
    // resourceExhausted. Clients must accept messages at least this large
    // (capnp::ReaderOptions::traversalLimitInWords); the default stays below
    // Cap'n Proto's own default client limit of 64 MiB.
    std::size_t max_payload_bytes = 48 * 1024 * 1024;
    // A connection that has not logged in by then is closed.
    std::chrono::seconds login_timeout{10};
    // Wrong passwords tolerated per connection.
    std::size_t max_login_failures = 3;
};

struct server_options {
    capnp_rpc::limits limits{};
    // Where the generated credential is written when rpc.user and rpc.password
    // are both empty. Created owner-only (see docs/capnproto-rpc.md); if that
    // cannot be done run() reports an error and does not listen.
    std::filesystem::path cookie_path = ".cookie";
    // Called on the owner strand once the listener accepts connections, with the
    // bound port (useful when rpc.port is 0). Must not block.
    std::function<void(std::uint16_t)> on_listening;
};

// Cap'n Proto RPC over TCP for the read-only chain API. The KJ event loop that
// Cap'n Proto requires lives on one thread owned by this object; it never
// touches chain state. All state, all handler calls and all cancellation belong
// to the owner strand, which is built on the executor given at construction.
// See docs/capnproto-rpc.md. Started only when rpc.enabled is true and the build
// has rpc=capnproto.
struct KND_API server {
    server(rpc_settings const& settings, reader& handlers,
        ::asio::any_io_executor executor, server_options options = {});
    ~server();
    server(server const&) = delete;
    server& operator=(server const&) = delete;

    // Bind the loopback listener and serve until stop(). Intended to be
    // co_spawned once. Returns after every operation and the KJ thread have been
    // joined; returns immediately, starting nothing, if stop() came first.
    ::asio::awaitable<void> run();

    // Bounded, non-blocking, thread-safe, idempotent; valid before run().
    void stop();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace kth::node::capnp_rpc

#endif // KTH_NODE_CAPNP_RPC_SERVER_HPP
