// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_HASH160_HPP
#define KTH_INFRASTUCTURE_CONFIG_HASH160_HPP

#include <iostream>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for a bitcoin 160 bit hash.
 */
struct KI_API hash160 {

    hash160() = default;
    hash160(hash160 const& x) = default;

    explicit
    hash160(std::string const& hexcode);

    explicit
    hash160(short_hash const& value);

    // explicit
    // hash160(byte_span value);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    explicit
    operator short_hash const&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, hash160& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, hash160 const& argument);

private:
    short_hash value_{null_short_hash};
};

} // namespace kth::infrastructure::config

#endif
