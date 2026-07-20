// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_HASH256_HPP
#define KTH_INFRASTUCTURE_CONFIG_HASH256_HPP

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a bitcoin 256-bit hash.
 *
 * Valid-by-construction: the only way to obtain a hash256 is
 * `parse_from` (returns `expect<hash256>`) or an explicit ctor over
 * a populated `hash_digest`. There is no default constructor — a
 * caller that needs "empty" semantics uses `std::optional<hash256>`.
 */
struct KI_API hash256 {
    using list = std::vector<hash256>;

    /**
     * Parse a 32-byte hex encoding. Returns `error::illegal_value` on
     * malformed input. No exceptions.
     */
    [[nodiscard]] static
    std::expected<hash256, kth::code> parse_from(std::string_view hexcode);

    /**
     * Wrap an already-populated `hash_digest`.
     */
    constexpr explicit
    hash256(hash_digest const& value) noexcept
        : value_(value) {}

    /**
     * Explicit accessor. No implicit cast to `hash_digest const&` —
     * that used to be a surprising conversion at overload sites.
     */
    [[nodiscard]] constexpr
    hash_digest const& value() const noexcept { return value_; }

    [[nodiscard]] constexpr
    bool operator==(hash256 const&) const noexcept = default;

    /**
     * Hex encoding used by `fmt::formatter<hash256>` and any caller
     * that needs the wire form.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    hash_digest value_;   // no default-init — set by every ctor
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::hash256> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::infrastructure::config::hash256 const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
