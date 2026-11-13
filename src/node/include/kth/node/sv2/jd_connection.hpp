// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_CONNECTION_HPP
#define KTH_NODE_SV2_JD_CONNECTION_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <system_error>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/strand.hpp>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/sv2/framing.hpp>

namespace kth::network {
class sam_session;
} // namespace kth::network

namespace kth::node::sv2 {

// How an upstream connection leaves the node. A pool learns the address a
// miner connects from, and a DNS server learns which pool a node asks for, so
// a node that keeps its peer connections behind Tor or I2P keeps these two
// there as well.
struct jd_route {
    // net.proxy, a numeric SOCKS5 address. When set, every upstream is reached
    // through it, its hostname goes to the proxy unresolved, and a failure of
    // the proxy fails the connection: nothing is dialed directly.
    std::string proxy;
    // net.proxy_randomize: fresh SOCKS5 credentials for each connection, which
    // Tor keeps on separate circuits.
    bool proxy_randomize = true;
    // The SAM session an .i2p upstream is dialed through. Not owned.
    network::sam_session* i2p = nullptr;
    // How long opening the stream may take, on top of the connection's own
    // deadline for the handshake. A Tor circuit to an onion service is built
    // in several round trips, and an I2P session builds its tunnels before
    // its first stream.
    std::chrono::milliseconds proxy_dial_timeout = std::chrono::seconds(20);
    std::chrono::milliseconds i2p_dial_timeout = std::chrono::seconds(90);
};

// One authenticated upstream, with no reconnect. The protocol owner supplies
// complete plaintext frames and consumes them on this connection's strand.
// It must join run() before destroying the connection or its handlers.
struct jd_connection {
    // Received frames are small JD replies and setup messages.
    static constexpr uint32_t max_payload_size = 1024 * 1024;
    // Sent frames may fill the whole SV2 frame: ProvideMissingTransactions.Success
    // carries raw transactions, and a frame's msg_length is a U24 (sv2-spec 3.2).
    static constexpr uint32_t max_send_payload_size = u24_max;
    static constexpr size_t max_queued_frames = 8;

    jd_connection(::asio::any_io_executor executor, std::string host,
        uint16_t port, hash_digest authority,
        std::chrono::milliseconds timeout = std::chrono::seconds(10),
        jd_route route = {});

    // A false handler result ends the connection. The return value explains
    // its terminal outcome; it is also delivered for failures before ready.
    [[nodiscard]] ::asio::awaitable<std::string> run(
        std::function<bool()> ready, std::function<bool(data_chunk)> received);

    // Neither the template producer nor a downstream miner waits for socket
    // space. A full queue is a terminal failure for the caller to handle.
    [[nodiscard]] bool send(data_chunk frame);
    void stop();

private:
    ::asio::awaitable<std::string> run_owned(
        std::function<bool()> ready, std::function<bool(data_chunk)> received);

    ::asio::strand<::asio::any_io_executor> owner_;
    std::string const host_;
    uint16_t const port_;
    hash_digest const authority_;
    std::chrono::milliseconds const timeout_;
    jd_route const route_;
    std::atomic_bool accepting_{true};
    std::atomic_bool started_{false};
    ::asio::experimental::concurrent_channel<void(std::error_code, data_chunk)> outgoing_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> stopping_;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_CONNECTION_HPP
