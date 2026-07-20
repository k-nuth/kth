// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_HEADER_HPP
#define KTH_HEADER_HPP

#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between base16 text and satoshi header.
 *
 * Valid-by-construction: no default ctor, no throwing ctor.
 */
struct KD_API header {

    /**
     * Parse a base16-encoded serialized header. Returns
     * `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    expect<header> parse_from(std::string_view text) noexcept;

    /**
     * Wrap an already-populated chain::header.
     */
    constexpr explicit
    header(chain::header const& value)
        : value_(value) {}

    /**
     * Explicit accessor.
     */
    [[nodiscard]] constexpr
    chain::header const& value() const noexcept { return value_; }

    /**
     * Base16 encoding used by `fmt::formatter<header>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::header value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::header> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::domain::config::header const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
