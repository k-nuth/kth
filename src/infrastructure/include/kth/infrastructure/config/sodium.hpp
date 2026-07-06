// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_SODIUM_HPP
#define KTH_INFRASTUCTURE_CONFIG_SODIUM_HPP

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base85 (Z85) sodium keys.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<sodium>`.
 */
struct KI_API sodium {
    using list = std::vector<sodium>;

    /**
     * Parse a Z85 (base85) encoded 32-byte key. Returns
     * `error::illegal_value` on malformed input or wrong length.
     */
    [[nodiscard]] static
    std::expected<sodium, kth::code> parse_from(std::string_view base85);

    /**
     * Wrap an already-populated `hash_digest`.
     */
    constexpr explicit
    sodium(hash_digest const& value) noexcept
        : value_(value) {}

    /**
     * Explicit accessor for the wrapped hash.
     */
    [[nodiscard]] constexpr
    hash_digest const& value() const noexcept { return value_; }

    /**
     * Base85 encoding used by `fmt::formatter<sodium>`.
     */
    [[nodiscard]]
    std::string to_string() const;

    [[nodiscard]] constexpr
    bool operator==(sodium const&) const noexcept = default;

private:
    hash_digest value_;
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::sodium>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::sodium const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
