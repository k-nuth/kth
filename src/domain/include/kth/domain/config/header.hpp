// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_HEADER_HPP
#define KTH_HEADER_HPP

#include <expected>
#include <string>
#include <system_error>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

/**
 * Serialization helper to convert between serialized and deserialized satoshi
 * header.
 */
struct KD_API header {
    header() = default;

    /**
     * Initialization constructor.
     * @param[in]  value  The value to initialize with.
     */
    header(chain::header const& value);

    /**
     * Copy constructor.
     * @param[in]  other  The object to copy into self on construct.
     */
    header(header const& x);

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type.
     */
    operator chain::header const&() const;

    /**
     * Parse a base16 string into a header object.
     * @param[in]  text  The base16 encoded string to parse.
     * @return           The parsed header object or an error.
     */
    [[nodiscard]] static
    std::expected<header, std::error_code> from_string(std::string_view text) noexcept;

    /**
     * Serialize the value to a base16 encoded string.
     * @return  The base16 encoded string.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    chain::header value_;
};

} // namespace kth::domain::config

#endif
