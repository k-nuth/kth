// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TRANSACTION_HPP
#define KTH_TRANSACTION_HPP

#include <expected>
#include <string>
#include <system_error>

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
     * Parse a base16 string into a transaction object.
     * @param[in]  text  The base16 encoded string to parse.
     * @return           The parsed transaction object or an error.
     */
    [[nodiscard]] static
    std::expected<transaction, std::error_code> from_string(std::string_view text) noexcept;

    /**
     * Serialize the value to a base16 encoded string.
     * @return  The base16 encoded string.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::transaction value_;
};

} // namespace kth::domain::config

#endif
