// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE58_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE58_HPP

#include <iostream>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base58 encoded text.
 */
struct KI_API base58 {

    base58() = default;
    base58(base58 const& x) = default;
    base58(base58&& x) = default;
    base58& operator=(base58 const&) = default;
    base58& operator=(base58&&) = default;


    explicit
    base58(std::string const& base58);

    explicit
    base58(data_chunk const& value);

    explicit
    base58(data_chunk&& value);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    explicit
    operator data_chunk const&() const;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to a generic data.
     */
    explicit
    operator data_slice() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, base58& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, const base58& argument);

private:
    data_chunk value_;
};

} // namespace kth::infrastructure::config

#endif
