// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_CHECKPOINT_HPP
#define KTH_INFRASTUCTURE_CONFIG_CHECKPOINT_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a blockchain checkpoint:
 * a `{block hash, block height}` tuple.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<checkpoint>`.
 */
struct KI_API checkpoint {
    using list = std::vector<checkpoint>;

    /**
     * Parse `hash[:height]` text form. Height defaults to zero.
     * Returns `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    std::expected<checkpoint, kth::code> parse_from(std::string_view value);

    /**
     * Parse `hash` (hex) and pair with a caller-supplied height.
     */
    [[nodiscard]] static
    std::expected<checkpoint, kth::code> parse_from(std::string_view hash, size_t height);

    /**
     * Wrap an already-decoded hash.
     */
    constexpr
    checkpoint(hash_digest const& hash, size_t height) noexcept
        : height_(height), hash_(hash) {}

    /**
     * True when `height` is at-or-below the last (highest) checkpoint.
     */
    [[nodiscard]] static
    bool covered(size_t height, list const& checks);

    /**
     * True unless the (hash, height) contradicts an entry in `checks`.
     */
    [[nodiscard]] static
    bool validate(hash_digest const& hash, size_t height, list const& checks);

    [[nodiscard]] constexpr
    hash_digest const& hash() const noexcept { return hash_; }

    [[nodiscard]] constexpr
    size_t height() const noexcept { return height_; }

    /**
     * `hash:height` encoding, used by `fmt::formatter<checkpoint>`.
     */
    [[nodiscard]]
    std::string to_string() const;

    // Ordering is (height, hash) — the field declaration order below
    // is chosen so the defaulted `<=>` compares that way for free.
    // No padding cost: `size_t` (8/align 8) + `hash_digest` (32/align 1)
    // total 40 bytes with alignment 8, same as the reverse order.
    [[nodiscard]]
    friend auto operator<=>(checkpoint const&, checkpoint const&) = default;

private:
    size_t      height_;
    hash_digest hash_;
};

} // namespace kth::infrastructure::config

template <>
struct fmt::formatter<kth::infrastructure::config::checkpoint>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::infrastructure::config::checkpoint const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
