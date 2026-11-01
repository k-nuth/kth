// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_GRPC_RPC_TRANSPORT_HPP
#define KTH_NODE_GRPC_RPC_TRANSPORT_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <asio/awaitable.hpp>

#include <kth/node/define.hpp>
#include <kth/rpc/v1/node.pb.h>

// The gRPC transport, and nothing else: it knows nothing about the chain. It
// accepts connections, authenticates and admits calls, hands each typed request
// to an async handler, and writes the typed answer (or status) back. Everything
// foreign (gRPC, asio-grpc, their threads) stays behind this header and inside
// transport.cpp; the public surface is asio + the generated protobuf messages.
//
// Ownership: a transport owns its listener, its completion queue and two
// threads (the one running the gRPC context, which is where handlers resume,
// and a controller that performs the blocking server shutdown). Nothing is
// detached: join() returns only after every in-flight handler has finished.

namespace kth::node::grpc_rpc {

enum class status_code {
    invalid_argument,
    not_found,
    resource_exhausted,
    unavailable,
    aborted,
    internal,
};

/// A failed call, carried as data. Maps one to one onto the grpc::StatusCode of
/// the same name; `message` is sent to the client verbatim.
struct rpc_status {
    status_code code;
    std::string message;
};

template <typename T>
using result = std::expected<T, rpc_status>;

/// A typed asynchronous handler. It is resumed on the transport's own thread and
/// may await anything (including work on another executor). The request is
/// owned by the transport and outlives the awaitable; do not keep a reference
/// to it afterwards. If the client cancels or the server shuts down, the
/// awaitable is cancelled (asio cancellation), so a handler that parks on an
/// asio operation unwinds without extra code.
template <typename Request, typename Response>
using handler = std::function<::asio::awaitable<result<Response>>(Request const&)>;

struct handlers {
    handler<::kth::rpc::v1::GetChainInfoRequest, ::kth::rpc::v1::GetChainInfoResponse> get_chain_info;
    handler<::kth::rpc::v1::GetBlockRequest, ::kth::rpc::v1::GetBlockResponse> get_block;
    handler<::kth::rpc::v1::GetTransactionRequest, ::kth::rpc::v1::GetTransactionResponse> get_transaction;
};

struct KND_API transport_options {
    /// Must be a loopback IP literal: the transport is plaintext, so it refuses to
    /// listen anywhere else rather than expose reads (and credentials) on a network.
    std::string bind{"127.0.0.1"};
    /// 0 asks the OS for a free port; see transport::port().
    std::uint16_t port{8332};
    /// HTTP-Basic style credentials, sent as `authorization: Basic base64(user:password)`
    /// call metadata. A transport without credentials refuses to start.
    std::string user;
    std::string password;
    /// Calls being served at once; the next one is refused with RESOURCE_EXHAUSTED.
    std::size_t max_in_flight{16};
    /// Largest request message accepted (requests here are a 32-byte hash).
    std::size_t max_request_bytes{4096};
    /// Largest response message sent; a bigger answer is RESOURCE_EXHAUSTED.
    std::size_t max_response_bytes{64u * 1024u * 1024u};
    /// On stop(), how long running calls may finish before they are cancelled.
    std::chrono::milliseconds shutdown_grace{1000};
};

class KND_API transport {
public:
    transport(transport_options options, handlers service);
    ~transport();

    transport(transport const&) = delete;
    transport& operator=(transport const&) = delete;

    /// Replace the credentials before start(). Returns false (and changes nothing)
    /// once the transport has started or stopped.
    [[nodiscard]]
    bool set_credentials(std::string user, std::string password);

    /// Bind, listen and begin serving; returns the bound port. Fails (and leaves
    /// the transport unusable) on a non-loopback bind, missing credentials, an
    /// unbindable address, a second start, or a start after stop().
    [[nodiscard]]
    std::expected<std::uint16_t, std::string> start();

    /// Ask the transport to wind down. Never blocks, idempotent, callable from any
    /// thread (including node executor threads) and before start().
    void stop();

    /// Block until the transport is fully down. Returns immediately if it never
    /// started. Must not be called from a handler or from the executor that
    /// handlers wait on.
    void join();

    /// The bound port; 0 until start() succeeds.
    [[nodiscard]]
    std::uint16_t port() const;

    /// Calls currently admitted.
    [[nodiscard]]
    std::size_t in_flight() const;

private:
    struct impl;

    enum class state { created, running, stopped };

    transport_options options_;
    handlers handlers_;

    // Control plane only: the call path never takes this lock.
    mutable std::mutex mutex_;
    std::mutex join_mutex_;
    std::condition_variable stop_cv_;
    state state_{state::created};
    bool joined_{false};

    std::unique_ptr<impl> impl_;
    std::thread grpc_thread_;
    std::thread controller_thread_;
    std::uint16_t port_{0};
    std::atomic<std::size_t> in_flight_{0};
};

} // namespace kth::node::grpc_rpc

#endif // KTH_NODE_GRPC_RPC_TRANSPORT_HPP
