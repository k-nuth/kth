// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_ENDORSEMENT_HPP
#define KTH_ENDORSEMENT_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/core.h>

#include <kth/domain.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between endorsement text and data_chunk.
 *
 * Valid-by-construction: no default ctor; string parsing goes through
 * `parse_from` returning `expect<endorsement>`.
 */
struct KD_API endorsement {

    /**
     * Parse a base16-encoded signature. Returns `error::illegal_value`
     * on malformed input or when the bytes exceed `max_endorsement_size`.
     */
    [[nodiscard]] static
    expect<endorsement> parse_from(std::string_view hexcode);

    /**
     * Wrap already-decoded bytes.
     */
    explicit
    endorsement(data_chunk value)
        : value_(std::move(value)) {}

    /**
     * Fixed-size byte-array convenience ctor. Marked `constexpr` so
     * static signatures can be materialised at compile time.
     */
    template <size_t Size>
    constexpr explicit
    endorsement(byte_array<Size> const& value)
        : value_(value.begin(), value.end()) {}

    /**
     * Explicit accessor.
     */
    [[nodiscard]]
    data_chunk const& value() const noexcept { return value_; }

    /**
     * Zero-copy byte-span view.
     */
    [[nodiscard]]
    byte_span as_span() const noexcept { return value_; }

    /**
     * Base16 encoding used by `fmt::formatter<endorsement>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    data_chunk value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::endorsement> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::domain::config::endorsement const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
