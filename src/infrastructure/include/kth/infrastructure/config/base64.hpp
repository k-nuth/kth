// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE64_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE64_HPP

#include <iostream>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base64 encoded data.
 */
struct KI_API base64 {
    base64() = default;

    explicit
    base64(std::string_view base64);

    explicit
    base64(data_chunk const& value);

    explicit
    base64(data_chunk&& value);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    [[nodiscard]] explicit
    operator data_chunk const&() const noexcept;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to a generic data.
     */
    [[nodiscard]] explicit
    operator data_slice() const noexcept;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, base64& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, base64 const& argument);

private:

    /**
     * The state of this object.
     */
    data_chunk value_;
};

} // namespace kth::infrastructure::config

#endif
