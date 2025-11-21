// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_ENDORSEMENT_HPP
#define KTH_ENDORSEMENT_HPP

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include <kth/domain.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between endorsement string and data_chunk.
 */
struct KD_API endorsement {
    endorsement() = default;

    /**
     * Initialization constructor.
     * @param[in]  hexcode  The value to initialize with.
     */
    endorsement(std::string const& hexcode);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    endorsement(data_chunk const& value);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    template <size_t Size>
    endorsement(byte_array<Size> const& value)
        : value_(value.begin(), value.end()) {
    }

    /**
     * Copy constructor.
     * @param[in]  other  The object to copy into self on construct.
     */
    endorsement(endorsement const& x);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator data_chunk const&() const;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to a generic data reference.
     */
    operator data_slice() const;

    /**
     * Overload stream in. If input is invalid sets no bytes in argument.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& input,
                                    endorsement& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend std::ostream& operator<<(std::ostream& output,
                                    const endorsement& argument);

private:
    /**
     * The state of this object.
     */
    data_chunk value_;
};

} // namespace kth::domain::config

#endif