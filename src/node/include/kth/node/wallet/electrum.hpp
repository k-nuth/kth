// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_ELECTRUM_HPP
#define KTH_NODE_WALLET_ELECTRUM_HPP

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <asio/awaitable.hpp>

#include <kth/domain/config/network.hpp>
#include <kth/node/define.hpp>
#include <kth/node/wallet/service.hpp>

namespace kth::node::wallet {

// A local Electrum-protocol listener: a compatibility layer only for wallets
// that do not speak the P2P protocol. It answers Fulcrum's methods, with
// Fulcrum's argument orders and result shapes, from the wallet support of the
// same process. It listens on a loopback address only: the listener is for
// the wallets on the same machine, and a node answering Electrum anywhere
// else would be a server that learns what other people's wallets watch. A
// wallet that can should talk P2P instead.

/// Where a listener listens: a loopback address and a port.
struct electrum_bind {
    /// Without brackets, also for IPv6.
    std::string address;
    uint16_t port;
};

/// ADDRESS:PORT ([ADDRESS]:PORT for IPv6) with a loopback address
/// (127.0.0.0/8 or ::1), or why not.
[[nodiscard]]
KND_API
std::expected<electrum_bind, std::string> parse_electrum_bind(std::string const& bind);

/// Why `bind` cannot be where a listener listens, or none.
[[nodiscard]]
KND_API
std::optional<std::string> electrum_bind_refusal(std::string const& bind);

class KND_API electrum_server {
public:
    electrum_server(service& wallet, domain::config::network network);
    ~electrum_server();
    electrum_server(electrum_server const&) = delete;
    electrum_server& operator=(electrum_server const&) = delete;

    /// Binds `address` (a loopback address) at `port` (0: a port the OS
    /// assigns) on the wallet's executor and returns the port it listens on.
    /// listen_failed otherwise, and for any address that is not a loopback
    /// one, which is never bound.
    [[nodiscard]] std::expected<uint16_t, std::error_code> listen(std::string const& address, uint16_t port);

    /// Accepts and answers clients until stop().
    ::asio::awaitable<void> run();

    /// Closes the listener and every client. Thread safe.
    void stop();

    /// The answer to one request line (a JSON-RPC request or a batch of them),
    /// without its newline. Empty for a notification.
    ::asio::awaitable<std::string> answer(std::string line);

private:
    struct impl;
    // Shared with the client coroutines, which may outlive a stop() by a moment.
    std::shared_ptr<impl> impl_;
};

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_ELECTRUM_HPP
