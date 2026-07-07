// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/endpoint.hpp>

#include <charconv>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::infrastructure::config {

// static
std::expected<endpoint, kth::code> endpoint::parse_from(std::string_view text) {
    static
    std::regex const regular(
        R"(^((tcp|udp|http|https|inproc):\/\/)?(\[([0-9a-f:\.]+)\]|([^:]+))(:([0-9]{1,5}))?$)");

    std::string const value{text};   // std::regex still needs a std::string
    std::sregex_iterator it(value.begin(), value.end(), regular), end;
    if (it == end) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const& match = *it;
    std::string scheme(match[2]);
    std::string host(match[3]);
    std::string const port_text(match[7]);

    uint16_t port = 0;
    if ( ! port_text.empty()) {
        auto const result = std::from_chars(
            port_text.data(), port_text.data() + port_text.size(), port);
        if (result.ec != std::errc()) {
            return std::unexpected(kth::error::illegal_value);
        }
    }

    return endpoint{std::move(scheme), std::move(host), port};
}

endpoint::endpoint(authority const& authority) {
    auto parsed = parse_from(authority.to_string());
    // authority::to_string() is always a valid endpoint text (host[:port]),
    // so this can't fail — enforce the invariant even in Release.
    KTH_CONTRACT(parsed.has_value());
    *this = std::move(*parsed);
}

#if ! defined(__EMSCRIPTEN__)
endpoint::endpoint(asio::endpoint const& host)
    : endpoint(host.address(), host.port())
{}

endpoint::endpoint(asio::address const& ip, uint16_t port)
    : host_(ip.to_string()), port_(port)
{}
#endif

std::string endpoint::to_string() const {
    if ( ! scheme_.empty() && port_ != 0) {
        return fmt::format("{}://{}:{}", scheme_, host_, port_);
    }
    if ( ! scheme_.empty()) {
        return fmt::format("{}://{}", scheme_, host_);
    }
    if (port_ != 0) {
        return fmt::format("{}:{}", host_, port_);
    }
    return host_;
}

bool endpoint::operator==(endpoint const& x) const {
    return host_ == x.host_ && port_ == x.port_ && scheme_ == x.scheme_;
}

} // namespace kth::infrastructure::config
