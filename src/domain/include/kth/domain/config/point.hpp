// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_POINT_HPP
#define KTH_POINT_HPP

#include <iostream>
#include <string>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between text and an output_point.
 */
struct KD_API point {
    static
    std::string const delimeter;

    point() = default;

    /**
     * Initialization constructor.
     * @param[in]  tuple  The value to initialize with.
     */
    point(std::string const& tuple);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    point(const chain::output_point& value);

    point(point const& x);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator const chain::output_point&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& input, point& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend std::ostream& operator<<(std::ostream& output,
                                    point const& argument);

private:
    /**
     * The state of this object.
     */
    chain::output_point value_;
};

} // namespace kth::domain::config

#endif
