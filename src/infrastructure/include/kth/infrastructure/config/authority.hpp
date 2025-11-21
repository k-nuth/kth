// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_AUTHORITY_HPP
#define KTH_INFRASTUCTURE_CONFIG_AUTHORITY_HPP

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// #include <fmt/ostream.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/message/network_address.hpp>


// #if ! defined(__EMSCRIPTEN__)
#include <kth/infrastructure/utility/asio.hpp>
// #endif

namespace kth::infrastructure::config {

/**
 * Serialization helper for a network authority.
 * This is a container for a {ip address, port} tuple.
 */
struct KI_API authority {
    using list = std::vector<authority>;


    authority() = default;
    authority(authority const& x) = default;
    authority(authority&& x) = default;
    authority& operator=(authority const& x) = default;
    authority& operator=(authority&& x) = default;


    explicit
    authority(std::string const& authority);

    //Note(fernando): in kth-network it is used the implicit convertion
    // implicit
    authority(message::network_address const& address);

    authority(message::ip_address const& ip, uint16_t port);


    authority(std::string const& host, uint16_t port);

#if ! defined(__EMSCRIPTEN__)
    authority(asio::address const& ip, uint16_t port);

    explicit
    authority(asio::endpoint const& endpoint);
#endif

    explicit
    operator bool const() const;


// #if ! defined(__EMSCRIPTEN__)
    asio::ipv6 asio_ip() const;
// #endif

    message::ip_address ip() const;
    uint16_t port() const;
    std::string to_hostname() const;
    std::string to_string() const;
    message::network_address to_network_address() const;

    bool operator==(authority const& x) const;
    bool operator!=(authority const& x) const;

    friend
    std::istream& operator>>(std::istream& input, authority& argument);
    friend
    std::ostream& operator<<(std::ostream& output, authority const& argument);

private:
// #if ! defined(__EMSCRIPTEN__)
    asio::ipv6 ip_;
// #endif
    uint16_t port_{0};
};

} // namespace kth::infrastructure::config

// template <> struct fmt::formatter<kth::infrastructure::config::authority> : ostream_formatter {};

template <>
struct fmt::formatter<kth::infrastructure::config::authority> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::authority const& value, FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};


#endif
