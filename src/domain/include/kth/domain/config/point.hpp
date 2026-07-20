// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_POINT_HPP
#define KTH_POINT_HPP

#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between text and an output_point.
 *
 * Valid-by-construction: no default ctor. Wire format `<txhash>:<index>`.
 */
struct KD_API point {
    static
    std::string_view const delimeter;

    /**
     * Parse the tuple. Returns `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    expect<point> parse_from(std::string_view text);

    /**
     * Wrap an already-populated chain::output_point.
     */
    constexpr explicit
    point(chain::output_point const& value)
        : value_(value) {}

    /**
     * Explicit accessor. No implicit cast to `chain::output_point const&` —
     * callers unwrap explicitly.
     */
    [[nodiscard]] constexpr
    chain::output_point const& value() const noexcept { return value_; }

    /**
     * Text form used by `fmt::formatter<point>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::output_point value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::point> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::domain::config::point const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
