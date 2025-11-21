// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE16_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE16_HPP

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base16 encoded data.
 */
//TODO(fernando): make a generic class for baseX
struct KI_API base16 {

    base16() = default;
    base16(base16 const& x) = default;
    base16(base16&& x) = default;
    base16& operator=(base16 const&) = default;
    base16& operator=(base16&&) = default;

    explicit
    base16(std::string const& hexcode);

    explicit
    base16(data_chunk const& value);

    explicit
    base16(data_chunk&& value);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    template <size_t Size>
    explicit
    base16(byte_array<Size> const& value)
        : value_(value.begin(), value.end()) {}

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    // implicit
    operator data_chunk const&() const;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to a generic data.
     */
    explicit
    operator data_slice() const;

    /**
     * Overload stream in. If input is invalid sets no bytes in argument.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, base16& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, base16 const& argument);

private:
    data_chunk value_;
};

} // namespace kth::infrastructure::config

#endif
