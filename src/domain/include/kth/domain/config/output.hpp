// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_OUTPUT_HPP
#define KTH_OUTPUT_HPP

#include <cstdint>
#include <iostream>
#include <string>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between a base58-string:number and
 * a vector of chain::output.
 */
struct KD_API output {

    output();

    /**
     * Initialization constructor.
     * @param[in]  tuple  The value to initialize with.
     */
    output(std::string const& tuple);

    /// Parsed properties
    [[nodiscard]]
    bool is_stealth() const;

    [[nodiscard]]
    uint64_t amount() const;

    [[nodiscard]]
    uint8_t version() const;

    [[nodiscard]]
    chain::script const& script() const;

    [[nodiscard]]
    short_hash const& pay_to_hash() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& input, output& argument);

private:
    /**
     * The transaction output state of this object.
     * This data is translated to an output given expected version information.
     */
    bool is_stealth_{false};
    uint64_t amount_{0};
    uint8_t version_{0};
    chain::script script_;
    short_hash pay_to_hash_;
};

} // namespace kth::domain::config

#endif
