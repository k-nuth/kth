// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE2_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE2_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base2-encoded binary data.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<base2>`.
 */
struct KI_API base2 {

    /**
     * Parse a base2 string. Returns `error::illegal_value` on non-`0/1`
     * characters.
     */
    [[nodiscard]] static
    std::expected<base2, kth::code> parse_from(std::string_view text) noexcept;

    /**
     * Wrap an already-populated `binary`.
     */
    explicit
    base2(binary const& value)
        : value_(value) {}

    /**
     * Explicit accessor for the wrapped value.
     */
    [[nodiscard]]
    binary const& value() const noexcept { return value_; }

    /**
     * Number of bits stored.
     */
    [[nodiscard]]
    size_t size() const noexcept { return value_.size(); }

    /**
     * Base2 encoding used by `fmt::formatter<base2>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    binary value_;
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::base2>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::base2 const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
