// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_FORMATS_BASE_10_HPP
#define KTH_INFRASTRUCTURE_FORMATS_BASE_10_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/fixed_string.hpp>
#include <expected>

namespace kth {

enum class base10_errc {
    invalid_format,
    excess_precision,
    overflow
};

/// Parse unsigned fixed-point decimal without allocation or floating point.
/// Empty text and a lone point retain their historical zero value.
/// strict=false rounds discarded nonzero fractions upward.
/// if consteval selects exact compile-time arithmetic; runtime uses from_chars.
[[nodiscard]] constexpr std::expected<uint64_t, base10_errc> decode_base10(
    std::string_view amount, uint8_t decimal_places = 0, bool strict = true) noexcept;

/// Exact decimal formatting with trimmed fractional zeros. All inputs are valid.
/// Runtime uses to_chars; constant evaluation uses integer arithmetic.
/// Allocates the result; allocation failures may throw.
[[nodiscard]] constexpr std::string encode_base10(uint64_t amount, uint8_t decimal_places = 0);

} // namespace kth

#include <kth/infrastructure/impl/formats/base_10.ipp>

#endif
