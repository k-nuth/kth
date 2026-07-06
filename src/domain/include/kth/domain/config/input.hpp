// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INPUT_HPP
#define KTH_INPUT_HPP

#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::config {

/**
 * Serialization helper stub for chain::input.
 *
 * Valid-by-construction: no default ctor. Callers obtain an instance
 * via `parse_from` or one of the explicit domain-type ctors. A caller
 * that needs "empty" semantics uses `std::optional<input>`.
 *
 * Wire format: `<txhash>:<index>[:<sequence>]`.
 */
struct KD_API input {

    /**
     * Parse the bx-tuple encoding. Returns `error::illegal_value` on
     * malformed input. No exceptions.
     */
    [[nodiscard]] static
    expect<input> parse_from(std::string_view text);

    /**
     * Wrap an already-populated chain::input.
     */
    constexpr explicit
    input(chain::input const& value)
        : value_(value) {}

    /**
     * Wrap a chain::input_point. Script defaults to empty; sequence
     * defaults to `max_input_sequence`.
     */
    explicit
    input(chain::input_point const& value)
        : value_(chain::input{value, {}, max_input_sequence}) {}

    /**
     * Explicit accessor. No implicit conversion — those used to make
     * overload resolution surprising.
     */
    [[nodiscard]] constexpr
    chain::input const& value() const noexcept { return value_; }

    /**
     * Bx-tuple encoding used by `fmt::formatter<input>` and callers
     * that need the wire representation.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::input value_;   // populated by every ctor / parse_from
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::input> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::domain::config::input const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
