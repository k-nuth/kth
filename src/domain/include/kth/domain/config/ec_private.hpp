// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONFIG_EC_PRIVATE_HPP
#define KTH_CONFIG_EC_PRIVATE_HPP

#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between a base16 string and `ec_secret`.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<ec_private>`.
 */
struct KD_API ec_private {

    /**
     * Parse a base16-encoded secret. Returns `error::illegal_value` on
     * malformed input or a value that fails EC curve verification.
     */
    [[nodiscard]] static
    expect<ec_private> parse_from(std::string_view hexcode);

    /**
     * Wrap an already-validated secret.
     */
    constexpr explicit
    ec_private(ec_secret const& secret) noexcept
        : value_(secret) {}

    /**
     * Explicit accessor for the wrapped secret.
     */
    [[nodiscard]] constexpr
    ec_secret const& value() const noexcept { return value_; }

    /**
     * Base16 encoding used by `fmt::formatter<ec_private>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    ec_secret value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::ec_private>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::domain::config::ec_private const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
