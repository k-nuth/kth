// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef KTH_NETWORK_WEBSOCKET_TRANSPORT_HPP
#define KTH_NETWORK_WEBSOCKET_TRANSPORT_HPP
#if defined(KTH_WITH_WEBSOCKET)
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>

namespace kth::network {
struct websocket_server_options {
    std::string path{"/"};
    std::vector<std::string> origins;
    std::chrono::seconds handshake_timeout{10};
    // Proxies (a TLS terminator) whose connections carry the client's address
    // in X-Forwarded-For. From any other address the header is ignored.
    std::vector<::asio::ip::address> trusted_proxies;
};

// All calls execute on the peer strand. The session owns the socket and this
// adapter; run() joins both codec reader and writer before session destruction.
class KN_API websocket_transport {
public:
    using result = std::pair<std::error_code, size_t>;
    explicit websocket_transport(::asio::ip::tcp::socket& socket);
    ~websocket_transport();
    websocket_transport(websocket_transport const&) = delete;
    websocket_transport& operator=(websocket_transport const&) = delete;
    ::asio::awaitable<std::error_code> accept(websocket_server_options const& options);
    ::asio::awaitable<void> run();
    ::asio::awaitable<result> read_exact(::asio::mutable_buffer buffer);
    ::asio::awaitable<result> write_exact(::asio::const_buffer buffer);
    void stop();
    bool clean_close() const;
    // The client behind a trusted proxy, from the X-Forwarded-For entry the
    // proxy appended; empty for a direct connection.
    std::optional<::asio::ip::address> forwarded_client() const;
private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
#endif
#endif
