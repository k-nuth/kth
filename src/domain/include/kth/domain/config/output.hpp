// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_OUTPUT_HPP
#define KTH_OUTPUT_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert a `target:amount[:seed]` tuple into a
 * transaction output. `target` may be a payment address, a stealth
 * address, or a serialized script (hex).
 *
 * Valid-by-construction: no default ctor, no throwing ctor. Fallible
 * string parsing goes through `parse_from` returning `expect<output>`.
 */
struct KD_API output {

    /**
     * Parse a `target:amount[:seed]` tuple. Returns `error::illegal_value`
     * on malformed input.
     */
    [[nodiscard]] static
    expect<output> parse_from(std::string_view tuple);

    [[nodiscard]]
    bool is_stealth() const { return is_stealth_; }

    [[nodiscard]]
    uint64_t amount() const { return amount_; }

    [[nodiscard]]
    uint8_t version() const { return version_; }

    [[nodiscard]]
    chain::script const& script() const { return script_; }

    [[nodiscard]]
    short_hash const& pay_to_hash() const { return pay_to_hash_; }

private:
    output(bool is_stealth, uint64_t amount, uint8_t version,
           chain::script script, short_hash pay_to_hash)
        : is_stealth_(is_stealth)
        , amount_(amount)
        , version_(version)
        , script_(std::move(script))
        , pay_to_hash_(pay_to_hash)
    {}

    bool is_stealth_;
    uint64_t amount_;
    uint8_t version_;
    chain::script script_;
    short_hash pay_to_hash_;
};

} // namespace kth::domain::config

#endif
