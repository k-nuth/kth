// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE16_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE16_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base16 encoded data.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<base16>`.
 */
//TODO(fernando): make a generic class for baseX
struct KI_API base16 {

    /**
     * Parse a base16 (hex) encoded string. Returns `error::illegal_value`
     * on non-hex characters or odd length.
     */
    [[nodiscard]] static
    std::expected<base16, kth::code> parse_from(std::string_view text) noexcept;

    /**
     * Wrap already-decoded bytes.
     */
    explicit
    base16(data_chunk value) : value_(std::move(value)) {}

    /**
     * Fixed-size byte-array convenience: wraps into a data_chunk.
     */
    template <size_t Size>
    explicit
    base16(byte_array<Size> const& value)
        : value_(value.begin(), value.end())
    {}

    /**
     * Explicit accessor for the underlying bytes.
     */
    [[nodiscard]]
    data_chunk const& value() const noexcept { return value_; }

    /**
     * Same bytes, as a non-owning span.
     */
    [[nodiscard]]
    byte_span as_span() const noexcept { return byte_span{value_}; }

    /**
     * Base16 encoding used by `fmt::formatter<base16>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    data_chunk value_;
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::base16>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::base16 const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
