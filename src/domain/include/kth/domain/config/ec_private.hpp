// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONFIG_EC_PRIVATE_HPP
#define KTH_CONFIG_EC_PRIVATE_HPP

#include <iostream>
#include <string>

#include <kth/domain.hpp>
#include <kth/domain/define.hpp>

#include <kth/infrastructure/math/elliptic_curve.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between base16 string and ec_secret.
 */
struct KD_API ec_private {

    ec_private() = default;

    /**
     * Initialization constructor.
     * @param[in]  hexcode  The value to initialize with.
     */
    ec_private(std::string const& hexcode);

    /**
     * Initialization constructor.
     * @param[in]  secret  The value to initialize with.
     */
    ec_private(ec_secret const& secret);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator ec_secret const&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, ec_private& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, ec_private const& argument);

private:
    /**
     * The state of this object.
     */
    ec_secret value_;
};

} // namespace kth::domain::config

#endif