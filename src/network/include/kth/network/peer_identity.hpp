// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PEER_IDENTITY_HPP
#define KTH_NETWORK_PEER_IDENTITY_HPP

#include <string>
#include <string_view>
#include <system_error>

#include <asio/ip/address.hpp>
#include <kth/infrastructure/config/endpoint.hpp>

namespace kth::network {

// Whether a destination is an IP literal. It decides what a SOCKS5 CONNECT
// carries, an address or a name for the proxy to resolve, and only a peer
// reached by its address is known by one.
[[nodiscard]] inline
bool is_ip_literal(std::string_view host) {
    if (host.size() > 1 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    std::error_code ec;
    ::asio::ip::make_address(std::string(host), ec);
    return ! ec;
}

[[nodiscard]] inline
infrastructure::config::endpoint canonical_peer_endpoint(
    infrastructure::config::endpoint const& destination)
{
    auto host = destination.host();
    if (host.size() > 1 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    std::error_code ec;
    auto const address = ::asio::ip::make_address(host, ec);
    if ( ! ec) {
        return infrastructure::config::endpoint(
            infrastructure::config::authority(address, destination.port()));
    }
    for (auto& c : host) {
        if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    }
    if (host.size() > 1 && host.back() == '.') host.pop_back();
    return infrastructure::config::endpoint(host, destination.port());
}

} // namespace kth::network

#endif // KTH_NETWORK_PEER_IDENTITY_HPP
