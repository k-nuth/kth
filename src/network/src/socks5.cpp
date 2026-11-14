// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/socks5.hpp>

#include <array>
#include <expected>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/ip/address.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::network {
namespace {

awaitable_expected<void> connect_and_negotiate(
    ::asio::ip::tcp::socket& socket,
    ::asio::ip::tcp::endpoint const& proxy,
    std::vector<uint8_t> const& request,
    bool randomize)
{
    try {
        co_await socket.async_connect(proxy, ::asio::use_awaitable);

        std::vector<uint8_t> greeting{0x05, 0x01, 0x00};
        if (randomize) {
            greeting[1] = 0x02;
            greeting.push_back(0x02);
        }
        co_await ::asio::async_write(socket, ::asio::buffer(greeting), ::asio::use_awaitable);

        std::array<uint8_t, 2> selection{};
        co_await ::asio::async_read(socket, ::asio::buffer(selection), ::asio::use_awaitable);
        if (selection[0] != 0x05 ||
            (selection[1] != 0x00 && ( ! randomize || selection[1] != 0x02))) {
            co_return std::unexpected(error::operation_failed);
        }

        if (selection[1] == 0x02) {
            auto const username = encode_base16(pseudo_random::generate<std::array<uint8_t, 32>>());
            auto const password = encode_base16(pseudo_random::generate<std::array<uint8_t, 32>>());
            std::vector<uint8_t> auth{0x01, uint8_t(username.size())};
            auth.insert(auth.end(), username.begin(), username.end());
            auth.push_back(uint8_t(password.size()));
            auth.insert(auth.end(), password.begin(), password.end());
            co_await ::asio::async_write(socket, ::asio::buffer(auth), ::asio::use_awaitable);

            std::array<uint8_t, 2> status{};
            co_await ::asio::async_read(socket, ::asio::buffer(status), ::asio::use_awaitable);
            if (status[0] != 0x01 || status[1] != 0x00) {
                co_return std::unexpected(error::operation_failed);
            }
        }

        co_await ::asio::async_write(socket, ::asio::buffer(request), ::asio::use_awaitable);

        std::array<uint8_t, 4> reply{};
        co_await ::asio::async_read(socket, ::asio::buffer(reply), ::asio::use_awaitable);
        if (reply[0] != 0x05 || reply[1] != 0x00 || reply[2] != 0x00) {
            co_return std::unexpected(error::operation_failed);
        }

        size_t remaining;
        switch (reply[3]) {
            case 0x01: remaining = 4 + 2; break;
            case 0x04: remaining = 16 + 2; break;
            case 0x03: {
                std::array<uint8_t, 1> length{};
                co_await ::asio::async_read(socket, ::asio::buffer(length), ::asio::use_awaitable);
                if (length[0] == 0) {
                    co_return std::unexpected(error::operation_failed);
                }
                remaining = size_t(length[0]) + 2;
                break;
            }
            default:
                co_return std::unexpected(error::operation_failed);
        }

        // BND.ADDR describes the proxy, not the peer. Consume it without using it
        // as an identity, and leave any following P2P bytes on the socket.
        std::array<uint8_t, 257> bound{};
        co_await ::asio::async_read(socket, ::asio::buffer(bound.data(), remaining), ::asio::use_awaitable);
        co_return std::expected<void, code>{};
    } catch (std::system_error const& e) {
        // Return transport errors as values so the deadline race completes on an
        // I/O failure as well as on a successful SOCKS exchange.
        co_return std::unexpected(error::boost_to_error_code(e.code()));
    }
}

} // namespace

awaitable_expected<::asio::ip::tcp::socket> async_socks5_connect(
    ::asio::any_io_executor executor,
    std::string const& proxy,
    std::string const& hostname,
    uint16_t port,
    bool randomize,
    std::chrono::steady_clock::duration timeout)
{
    auto const proxy_address = infrastructure::config::authority::parse_from(proxy);
    if ( ! proxy_address || proxy_address->port() == 0 || port == 0 ||
         hostname.empty() || hostname.size() > 255 ||
         hostname.find('\0') != std::string::npos || hostname.find('%') != std::string::npos) {
        co_return std::unexpected(error::illegal_value);
    }

    auto host = hostname;
    if (host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    std::error_code parse_ec;
    auto const address = ::asio::ip::make_address(host, parse_ec);
    std::vector<uint8_t> request{0x05, 0x01, 0x00};
    if ( ! parse_ec && address.is_v4()) {
        request.push_back(0x01);
        auto const bytes = address.to_v4().to_bytes();
        request.insert(request.end(), bytes.begin(), bytes.end());
    } else if ( ! parse_ec) {
        request.push_back(0x04);
        auto const bytes = address.to_v6().to_bytes();
        request.insert(request.end(), bytes.begin(), bytes.end());
    } else {
        request.push_back(0x03);
        request.push_back(uint8_t(hostname.size()));
        request.insert(request.end(), hostname.begin(), hostname.end());
    }
    request.push_back(uint8_t(port >> 8));
    request.push_back(uint8_t(port & 0xff));

    // Parsing the proxy as an authority accepts only numeric addresses. No
    // resolver is involved for either the proxy or the destination.
    auto const proxy_ip = proxy_address->asio_ip();
    auto const proxy_endpoint = ::asio::ip::tcp::endpoint(
        ::asio::ip::make_address(proxy_ip.is_v4_mapped()
            ? proxy_address->to_hostname() : proxy_ip.to_string()),
        proxy_address->port());
    ::asio::ip::tcp::socket socket(executor);
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);
    using namespace ::asio::experimental::awaitable_operators;
    auto result = co_await (
        connect_and_negotiate(socket, proxy_endpoint, request, randomize) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );
    if (result.index() == 1) {
        co_return std::unexpected(error::channel_timeout);
    }
    auto const& negotiated = std::get<0>(result);
    if ( ! negotiated) {
        co_return std::unexpected(negotiated.error());
    }
    co_return std::move(socket);
}

} // namespace kth::network
