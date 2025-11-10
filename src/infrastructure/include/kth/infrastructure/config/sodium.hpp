// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_SODIUM_HPP
#define KTH_INFRASTUCTURE_CONFIG_SODIUM_HPP

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base58 sodium keys.
 */
class KI_API sodium
{
public:
    using list = std::vector<sodium>;


    sodium();

    /**
     * Initialization constructor.
     * @param[in]  base85  The value to initialize with.
     */
    explicit
    sodium(std::string_view base85);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    explicit
    sodium(hash_digest const& value);

    /**
     * Copy constructor.
     * @param[in]  other  The object to copy into self on construct.
     */
    sodium(sodium const& x);

    /**
     * Getter.
     * @return True if the key is initialized.
     */
    explicit
    operator bool const() const;

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    explicit
    operator hash_digest const&() const;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to generic data.
     */
    explicit
    operator data_slice() const;

    /**
     * Get the key as a base85 encoded (z85) string.
     * @return The encoded key.
     */
    std::string to_string() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend
    std::istream& operator>>(std::istream& input, sodium& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend
    std::ostream& operator<<(std::ostream& output, const sodium& argument);

private:

    /**
     * The state of this object.
     */
    hash_digest value_;
};

} // namespace kth::infrastructure::config

#endif
