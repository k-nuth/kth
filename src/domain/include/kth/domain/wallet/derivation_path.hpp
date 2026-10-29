// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_DERIVATION_PATH_HPP
#define KTH_DOMAIN_WALLET_DERIVATION_PATH_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::domain::wallet {

enum class derivation_path_error {
    invalid_format,
    index_out_of_range,
    depth_exceeded,
    insufficient_space,
};

/// Parse an absolute m[/index...] or relative index[/index...] BIP32 path.
/// Components are decimal values in [0, 2^31), optionally hardened by a trailing apostrophe.
/// No signs, whitespace, empty components, partial numbers or more than 255 levels are accepted.
/// The root m has no components. An empty string is invalid.
/// The output span must not overlap the input string.
/// Returns the written count. On failure, the output may contain partial results and must not be used.
/// This overload performs no allocations and is usable in constant expressions.
[[nodiscard]] constexpr
std::expected<size_t, derivation_path_error> parse_derivation_path(std::string_view path, std::span<uint32_t> output) noexcept {
    constexpr uint32_t hardened = uint32_t{1} << 31;
    constexpr uint32_t max_index = hardened - 1;
    if (path == "m") return 0;
    if (path.starts_with("m/")) path.remove_prefix(2);
    if (path.empty()) return std::unexpected(derivation_path_error::invalid_format);

    size_t count = 0;
    size_t position = 0;
    while (true) {
        if (count == 255) return std::unexpected(derivation_path_error::depth_exceeded);
        if (count == output.size()) return std::unexpected(derivation_path_error::insufficient_space);
        uint32_t index = 0;
        auto const start = position;
        while (position < path.size() && path[position] >= '0' && path[position] <= '9') {
            uint32_t const digit = static_cast<uint32_t>(path[position] - '0');
            if (index > (max_index - digit) / 10) return std::unexpected(derivation_path_error::index_out_of_range);
            index = index * 10 + digit;
            ++position;
        }
        if (position == start) return std::unexpected(derivation_path_error::invalid_format);
        if (position < path.size() && path[position] == '\'') {
            index |= hardened;
            ++position;
        }
        output[count++] = index;
        if (position == path.size()) break;
        if (path[position++] != '/') return std::unexpected(derivation_path_error::invalid_format);
        if (position == path.size()) return std::unexpected(derivation_path_error::invalid_format);
    }
    return count;
}

/// Owning runtime adapter. Allocates only after the complete path has been validated.
[[nodiscard]] KD_API
expect<std::vector<uint32_t>> parse_derivation_path(std::string_view path);

} // namespace kth::domain::wallet

#endif
