// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INPUT_HPP
#define KTH_INPUT_HPP

#include <iostream>
#include <string>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

/**
 * Serialization helper stub for chain::input.
 */
struct KD_API input {
    input() = default;

    /**
     * Initialization constructor.
     * @param[in]  tuple  The value to initialize with.
     */
    input(std::string const& tuple);

    /**
     * Initialization constructor. Only the point is retained.
     * @param[in]  value  The value to initialize with.
     */
    input(chain::input const& value);

    input(input const& x);

    /**
     * Initialization constructor. Aspects of the input x than the point
     * are defaulted.
     * @param[in]  value  The value to initialize with.
     */
    input(chain::input_point const& value);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator chain::input const&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& stream, input& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend std::ostream& operator<<(std::ostream& output,
                                    input const& argument);

private:
    /**
     * The state of this object.
     */
    chain::input value_;
};

} // namespace kth::domain::config

#endif
