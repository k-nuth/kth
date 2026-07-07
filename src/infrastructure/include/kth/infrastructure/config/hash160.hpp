// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_HASH160_HPP
#define KTH_INFRASTUCTURE_CONFIG_HASH160_HPP

#include <expected>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a bitcoin 160-bit hash.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. String
 * parsing goes through `parse_from` returning `expect<hash160>`.
 */
struct KI_API hash160 {

    /**
     * Parse a 20-byte hex encoding. Returns `error::illegal_value` on
     * malformed input.
     */
    [[nodiscard]] static
    std::expected<hash160, kth::code> parse_from(std::string_view hexcode);

    /**
     * Wrap an already-populated `short_hash`.
     */
    constexpr explicit
    hash160(short_hash const& value) noexcept
        : value_(value) {}

    /**
     * Explicit accessor.
     */
    [[nodiscard]] constexpr
    short_hash const& value() const noexcept { return value_; }

    [[nodiscard]] constexpr
    bool operator==(hash160 const&) const noexcept = default;

    /**
     * Hex encoding used by `fmt::formatter<hash160>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    short_hash value_;
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::hash160> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::infrastructure::config::hash160 const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
