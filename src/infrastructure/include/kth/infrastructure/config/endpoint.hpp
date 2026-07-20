// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_ENDPOINT_HPP
#define KTH_INFRASTUCTURE_CONFIG_ENDPOINT_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a network endpoint in URI format:
 * `[scheme://]host[:port]`.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<endpoint>`.
 */
struct KI_API endpoint {
    using list = std::vector<endpoint>;

    /**
     * Parse the `[scheme://]host[:port]` form. Returns
     * `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    std::expected<endpoint, kth::code> parse_from(std::string_view text);

    /**
     * Wrap an authority (host+port, no scheme).
     */
    explicit
    endpoint(authority const& authority);

    endpoint(std::string_view host, uint16_t port)
        : host_(host), port_(port)
    {}

#if ! defined(__EMSCRIPTEN__)
    explicit
    endpoint(asio::endpoint const& host);

    endpoint(asio::address const& ip, uint16_t port);
#endif

    [[nodiscard]] std::string const& scheme() const { return scheme_; }
    [[nodiscard]] std::string const& host()   const { return host_; }
    [[nodiscard]] uint16_t           port()   const { return port_; }

    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]]
    bool operator==(endpoint const& x) const;

private:
    // Private "populate all fields" ctor for use by parse_from.
    endpoint(std::string scheme, std::string host, uint16_t port)
        : scheme_(std::move(scheme))
        , host_(std::move(host))
        , port_(port)
    {}

    std::string scheme_;
    std::string host_;
    uint16_t    port_{0};
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::endpoint>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::endpoint const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
