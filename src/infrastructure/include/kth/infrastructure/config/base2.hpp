// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE2_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE2_HPP

#include <cstddef>
#include <iostream>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base2 encoded data.
 */
struct KI_API base2 {

    base2() = default;
    base2(base2 const& x) = default;
    base2(base2&& x) = default;
    base2& operator=(base2 const&) = default;
    base2& operator=(base2&&) = default;

    /**
     * Initialization constructor.
     * @param[in]  bin  The value to initialize with.
     */
    explicit
    base2(std::string const& binary);

    /**
     * @param[in]  value  The value to initialize with.
     */
    explicit
    base2(binary const& value);


    /**
     * Get number of bits in value.
     */
    size_t size() const;

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    explicit
    operator binary const&() const;

    /**
     * Overload stream in. If input is invalid sets no bytes in argument.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, base2& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, const base2& argument);

private:
    binary value_;
};

} // namespace kth::infrastructure::config

#endif
