// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SOCKS5_HPP
#define KTH_NETWORK_SOCKS5_HPP

#include <chrono>
#include <cstdint>
#include <string>

#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

/// The proxy must be a numeric IP and port. Hostnames are resolved by SOCKS5.
/// The deadline covers both the TCP connection and the SOCKS5 exchange.
[[nodiscard]]
KN_API awaitable_expected<::asio::ip::tcp::socket> async_socks5_connect(
    ::asio::any_io_executor executor,
    std::string const& proxy,
    std::string const& hostname,
    uint16_t port,
    bool randomize,
    std::chrono::steady_clock::duration timeout);

} // namespace kth::network

#endif // KTH_NETWORK_SOCKS5_HPP
