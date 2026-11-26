// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/socks5.hpp>

#include <array>
#include <expected>
#include <span>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/ip/address.hpp>
#include <asio/read.hpp>
#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::network {
namespace {

// Every operation reports its error as a value. A failed or cancelled one makes
// the exchange return at once, so the deadline race in async_socks5_connect
// completes on an I/O failure as it does on a finished exchange, and nothing
// here throws.
awaitable_expected<void> send(::asio::ip::tcp::socket& socket, std::span<uint8_t const> bytes) {
    auto const [ec, sent] = co_await ::asio::async_write(socket,
        ::asio::buffer(bytes.data(), bytes.size()), ::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return std::unexpected(error::boost_to_error_code(ec));
    }
    co_return std::expected<void, code>{};
}

awaitable_expected<void> receive(::asio::ip::tcp::socket& socket, std::span<uint8_t> bytes) {
    auto const [ec, received] = co_await ::asio::async_read(socket,
        ::asio::buffer(bytes.data(), bytes.size()), ::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return std::unexpected(error::boost_to_error_code(ec));
    }
    co_return std::expected<void, code>{};
}

awaitable_expected<void> connect_and_negotiate(
    ::asio::ip::tcp::socket& socket,
    ::asio::ip::tcp::endpoint const& proxy,
    std::vector<uint8_t> const& request,
    bool randomize)
{
    auto const [connect_ec] = co_await socket.async_connect(proxy,
        ::asio::as_tuple(::asio::use_awaitable));
    if (connect_ec) {
        co_return std::unexpected(error::boost_to_error_code(connect_ec));
    }

    std::vector<uint8_t> greeting{0x05, 0x01, 0x00};
    if (randomize) {
        greeting[1] = 0x02;
        greeting.push_back(0x02);
    }
    if (auto const sent = co_await send(socket, greeting); ! sent) {
        co_return std::unexpected(sent.error());
    }

    std::array<uint8_t, 2> selection{};
    if (auto const read = co_await receive(socket, selection); ! read) {
        co_return std::unexpected(read.error());
    }
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
        if (auto const sent = co_await send(socket, auth); ! sent) {
            co_return std::unexpected(sent.error());
        }

        std::array<uint8_t, 2> status{};
        if (auto const read = co_await receive(socket, status); ! read) {
            co_return std::unexpected(read.error());
        }
        if (status[0] != 0x01 || status[1] != 0x00) {
            co_return std::unexpected(error::operation_failed);
        }
    }

    if (auto const sent = co_await send(socket, request); ! sent) {
        co_return std::unexpected(sent.error());
    }

    std::array<uint8_t, 4> reply{};
    if (auto const read = co_await receive(socket, reply); ! read) {
        co_return std::unexpected(read.error());
    }
    if (reply[0] != 0x05 || reply[1] != 0x00 || reply[2] != 0x00) {
        co_return std::unexpected(error::operation_failed);
    }

    size_t remaining = 0;
    switch (reply[3]) {
        case 0x01: remaining = 4 + 2; break;
        case 0x04: remaining = 16 + 2; break;
        case 0x03: {
            std::array<uint8_t, 1> length{};
            if (auto const read = co_await receive(socket, length); ! read) {
                co_return std::unexpected(read.error());
            }
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
    if (auto const read = co_await receive(socket, std::span<uint8_t>(bound.data(), remaining)); ! read) {
        co_return std::unexpected(read.error());
    }
    co_return std::expected<void, code>{};
}

} // namespace

awaitable_expected<::asio::ip::tcp::socket> async_socks5_connect(
    ::asio::any_io_executor executor,
    std::string proxy,
    std::string hostname,
    uint16_t port,
    bool randomize,
    std::chrono::steady_clock::duration timeout)
{
    auto const proxy_address = infrastructure::config::authority::parse_from(proxy);
    if ( ! proxy_address || proxy_address->is_overlay() || proxy_address->port() == 0 || port == 0 ||
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
    std::error_code proxy_ec;
    auto const proxy_ip_address = ::asio::ip::make_address(proxy_ip.is_v4_mapped()
        ? proxy_address->to_hostname() : proxy_ip.to_string(), proxy_ec);
    if (proxy_ec) {
        co_return std::unexpected(error::illegal_value);
    }
    auto const proxy_endpoint = ::asio::ip::tcp::endpoint(proxy_ip_address, proxy_address->port());
    // The exchange and its deadline complete without synchronisation between
    // them, so asio requires both to run on a strand; the caller's executor is
    // usually the thread pool, so the exchange gets its own strand here.
    ::asio::ip::tcp::socket socket(executor);
    ::asio::strand<::asio::any_io_executor> exchange_strand(executor);
    auto const negotiated = co_await ::asio::co_spawn(exchange_strand,
        [&]() -> awaitable_expected<void> {
            kth::steady_timer timer(exchange_strand);
            timer.expires_after(timeout);
            using namespace ::asio::experimental::awaitable_operators;
            auto result = co_await (
                connect_and_negotiate(socket, proxy_endpoint, request, randomize) ||
                timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
            );
            if (result.index() == 0) {
                co_return std::get<0>(std::move(result));
            }
            // A timer that was cancelled did not expire.
            auto const [timer_ec] = std::get<1>(result);
            if (timer_ec) {
                co_return std::unexpected(error::boost_to_error_code(timer_ec));
            }
            co_return std::unexpected(error::channel_timeout);
        },
        ::asio::use_awaitable);
    if ( ! negotiated) {
        co_return std::unexpected(negotiated.error());
    }
    co_return std::move(socket);
}

} // namespace kth::network
