// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/authority.hpp>

#include <algorithm>
#include <charconv>
#include <sstream>

#include <ctre.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

// #if defined(KTH_LOG_LIBRARY_BINLOG)
// #include <binlog/binlog.hpp>
// #endif

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::infrastructure::config {


#if ! defined(__EMSCRIPTEN__)
using namespace boost::program_options;
#endif

// host:    [2001:db8::2] or  2001:db8::2  or 1.2.240.1
// returns: [2001:db8::2] or [2001:db8::2] or 1.2.240.1
static
std::string to_host_name(std::string const& host) {
    if (host.find(':') == std::string::npos || host.find('[') == 0) {
        return host;
    }

    // auto const hostname = boost::format("[%1%]") % host;
    // return hostname.str();
    return fmt::format("[{}]", host);
}

// host: [2001:db8::2] or 2001:db8::2 or 1.2.240.1
static
std::string to_authority(std::string const& host, uint16_t port) {
    std::stringstream authority;
    authority << to_host_name(host);
    if (port > 0) {
        authority << ":" << port;
    }

    return authority.str();
}

static
std::string to_ipv6(std::string const& ipv4_address) {
    return std::string("::ffff:") + ipv4_address;
}

#if ! defined(__EMSCRIPTEN__)
static
asio::ipv6 to_ipv6(asio::ipv4 const& ipv4_address) {
    // Create an IPv6 mapped IPv4 address via serialization.
    auto const ipv6 = to_ipv6(ipv4_address.to_string());
    return asio::ipv6::from_string(ipv6);
}

static
asio::ipv6 to_ipv6(asio::address const& ip_address) {
    if (ip_address.is_v6()) {
        return ip_address.to_v6();
    }

    KTH_ASSERT_MSG(ip_address.is_v4(), "The address must be either IPv4 or IPv6.");
    return to_ipv6(ip_address.to_v4());
}

#endif // ! defined(__EMSCRIPTEN__)


static
std::string to_ipv4_hostname(asio::address const& ip_address) {
#if ! defined(__EMSCRIPTEN__)
    constexpr auto pattern = ctll::fixed_string{"^::ffff:([0-9\\.]+)$"};

    auto const address = ip_address.to_string();

    if (auto m = ctre::match<pattern>(address)) {
        return std::string(m.get<1>());
    }
#endif // ! defined(__EMSCRIPTEN__)

    return "";
}

static
std::string to_ipv6_hostname(asio::address const& ip_address) {
#if ! defined(__EMSCRIPTEN__)
    // IPv6 URLs use a bracketed IPv6 address, see rfc2732.
    // auto const hostname = boost::format("[%1%]") % to_ipv6(ip_address);
    // return hostname.str();
    return fmt::format("[{}]", to_ipv6(ip_address));
#else
    return "";
#endif // ! defined(__EMSCRIPTEN__)
}

// authority::authority(authority const& x)
//     : authority(x.ip_, x.port_)
// {}

// authority: [2001:db8::2]:port or 1.2.240.1:port
authority::authority(std::string const& authority) {
    std::stringstream(authority) >> *this;
}

// This is the format returned from peers on the bitcoin network.
authority::authority(message::network_address const& address)
    : authority(address.ip(), address.port())
{}

#if ! defined(__EMSCRIPTEN__)
static
asio::ipv6 to_boost_address(message::ip_address const& in) {
    asio::ipv6::bytes_type bytes;
    KTH_ASSERT(bytes.size() == in.size());
    std::copy_n(in.begin(), in.size(), bytes.begin());
    const asio::ipv6 out(bytes);
    return out;
}

static
message::ip_address to_bc_address(const asio::ipv6& in) {
    message::ip_address out;
    auto const bytes = in.to_bytes();
    KTH_ASSERT(bytes.size() == out.size());
    std::copy_n(bytes.begin(), bytes.size(), out.begin());
    return out;
}

#else

static
asio::ipv6 to_boost_address(message::ip_address const& in) {
    return 0;
}

static
message::ip_address to_bc_address(const asio::ipv6& in) {
    message::ip_address out;
    return out;
}

#endif // ! defined(__EMSCRIPTEN__)

authority::authority(message::ip_address const& ip, uint16_t port)
    : ip_(to_boost_address(ip)), port_(port)
{}

// host: [2001:db8::2] or 2001:db8::2 or 1.2.240.1
authority::authority(std::string const& host, uint16_t port)
    : authority(to_authority(host, port))
{}

#if ! defined(__EMSCRIPTEN__)
authority::authority(asio::address const& ip, uint16_t port)
    : ip_(to_ipv6(ip)), port_(port)
{}

authority::authority(asio::endpoint const& endpoint)
    : authority(endpoint.address(), endpoint.port())
{}
#endif

authority::operator bool const() const {
    return port_ != 0;
}

// #if ! defined(__EMSCRIPTEN__)
asio::ipv6 authority::asio_ip() const {
    return ip_;
}
// #endif

message::ip_address authority::ip() const {
    return to_bc_address(ip_);
}

uint16_t authority::port() const {
    return port_;
}

std::string authority::to_hostname() const {
    auto ipv4_hostname = to_ipv4_hostname(ip_);
    return ipv4_hostname.empty() ? to_ipv6_hostname(ip_) : ipv4_hostname;
}

message::network_address authority::to_network_address() const {
    static constexpr uint32_t services = 0;
    static constexpr uint32_t timestamp = 0;
    message::network_address const addr {
        timestamp, services, ip(), port(),
    };

    return addr;
}

std::string authority::to_string() const {
    std::stringstream value;
    value << *this;
    return value.str();
}

bool authority::operator==(authority const& x) const {
    return port() == x.port() && ip() == x.ip();
}

bool authority::operator!=(authority const& x) const {
    return !(*this == x);
}

std::istream& operator>>(std::istream& input, authority& argument) {
    using namespace ctre::literals;

    std::string value;
    input >> value;

    constexpr auto pattern = ctll::fixed_string{R"(^(([0-9\.]+)|\[([0-9a-f:\.]+)\])(:([0-9]{1,5}))?$)"};

    if (auto [whole, _0, ipv4, ipv6, _1, port] = ctre::match<pattern>(value); whole) {
        std::string ip_address = ipv6 ? ipv6.to_string() : to_ipv6(ipv4.to_string());

#if ! defined(__EMSCRIPTEN__)
        try {
            argument.ip_ = asio::ipv6::from_string(ip_address);
        } catch (std::exception const&) {
            BOOST_THROW_EXCEPTION(invalid_option_value(value));
        }
#endif

        if (port) {
            auto const port_sv = port.to_view();
            auto const result = std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), argument.port_);
            if (result.ec != std::errc()) {
#if ! defined(__EMSCRIPTEN__)
                BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
                throw std::invalid_argument(value);
#endif
            }
        } else {
            argument.port_ = 0;
        }
    } else {
#if ! defined(__EMSCRIPTEN__)
        BOOST_THROW_EXCEPTION(invalid_option_value(value));
#else
        throw std::invalid_argument(value);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, authority const& argument) {
    output << to_authority(argument.to_hostname(), argument.port());
    return output;
}

} // namespace kth::infrastructure::config
