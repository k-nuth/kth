// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TRANSACTION_HPP
#define KTH_TRANSACTION_HPP

#include <string>
#include <string_view>
#include <utility>

#include <fmt/core.h>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between a serialized (hex-encoded) satoshi
 * transaction and `chain::transaction`.
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<transaction>`.
 */
struct KD_API transaction {

    /**
     * Parse a base16-encoded, wire-formatted transaction. Returns
     * `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    expect<transaction> parse_from(std::string_view text) noexcept;

    /**
     * Wrap an already-populated `chain::transaction`.
     */
    explicit
    transaction(chain::transaction value)
        : value_(std::move(value)) {}

    [[nodiscard]]
    chain::transaction const& value() const noexcept { return value_; }

    /**
     * Base16 encoding used by `fmt::formatter<transaction>`.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::transaction value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::transaction>
    : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(kth::domain::config::transaction const& value,
                FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(value.to_string(), ctx);
    }
};

#endif
