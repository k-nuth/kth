// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TRANSACTION_HPP
#define KTH_TRANSACTION_HPP

#include <iostream>
#include <string>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between serialized and deserialized satoshi
 * transaction.
 */
struct KD_API transaction {
    transaction() = default;

    /**
     * Initialization constructor.
     * @param[in]  hexcode  The value to initialize with.
     */
    transaction(std::string const& hexcode);

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    transaction(chain::transaction const& value);

    /**
     * Copy constructor.
     * @param[in]  other  The object to copy into self on construct.
     */
    transaction(transaction const& x);

    /**
     * Return a reference to the data member.
     * @return  A reference to the object's internal data.
     */
    chain::transaction& data();

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator chain::transaction const&() const;

    /**
     * Overload stream in. Throws if input is invalid.
     * @param[in]   input     The input stream to read the value from.
     * @param[out]  argument  The object to receive the read value.
     * @return                The input stream reference.
     */
    friend std::istream& operator>>(std::istream& input,
                                    transaction& argument);

    /**
     * Overload stream out.
     * @param[in]   output    The output stream to write the value to.
     * @param[out]  argument  The object from which to obtain the value.
     * @return                The output stream reference.
     */
    friend std::ostream& operator<<(std::ostream& output,
                                    transaction const& argument);

private:
    /**
     * The state of this object's transaction data.
     */
    chain::transaction value_;
};

} // namespace kth::domain::config

#endif
