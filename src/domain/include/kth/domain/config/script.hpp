// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONFIG_SCRIPT_HPP
#define KTH_CONFIG_SCRIPT_HPP

#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between base16 / mnemonic / raw script
 * and chain::script.
 *
 * Valid-by-construction: no default ctor. Every fallible source
 * (mnemonic string, raw bytes, joined token list) is a named factory
 * that returns `expect<script>` — no throwing constructors.
 */
struct KD_API script {

    /**
     * Parse a whitespace-separated mnemonic (e.g. `"OP_DUP OP_HASH160 ..."`).
     * Returns `error::illegal_value` on malformed input.
     */
    [[nodiscard]] static
    expect<script> parse_from(std::string_view mnemonic);

    /**
     * Deserialize from the wire byte encoding.
     */
    [[nodiscard]] static
    expect<script> from_data_chunk(data_chunk const& value);

    /**
     * Same as `parse_from` after joining the tokens with spaces.
     */
    [[nodiscard]] static
    expect<script> parse_tokens(std::vector<std::string> const& tokens);

    /**
     * Wrap an already-populated chain::script.
     */
    constexpr explicit
    script(chain::script const& value)
        : value_(value) {}

    /**
     * Serialize the script to bytes according to the wire protocol.
     */
    [[nodiscard]]
    kth::data_chunk to_data() const;

    /**
     * Return the mnemonic form of the script.
     */
    [[nodiscard]]
    std::string to_string() const;

    /**
     * Explicit accessor.
     */
    [[nodiscard]] constexpr
    chain::script const& value() const noexcept { return value_; }

private:
    chain::script value_;
};

} // namespace kth::domain::config

template <>
struct fmt::formatter<kth::domain::config::script> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(kth::domain::config::script const& value,
                format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", value.to_string());
    }
};

#endif
