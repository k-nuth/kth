// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE58_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE58_HPP

#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base58 encoded text.
 */
struct KI_API base58 {
    base58() = default;

    explicit
    base58(data_chunk const& value);

    explicit
    base58(data_chunk&& value);

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
    operator byte_span() const noexcept;

    /**
     * Get the underlying data.
     * @return  Reference to the internal data.
     */
    [[nodiscard]]
    data_chunk const& data() const noexcept;

    /**
     * Get the underlying data as a span.
     * @return  The internal data as a span.
     */
    [[nodiscard]]
    byte_span as_span() const noexcept;

    /**
     * Parse a base58 string into a base58 object.
     * @param[in]  text  The base58 encoded string to parse.
     * @return           The parsed base58 object or an error.
     */
    [[nodiscard]] static
    std::expected<base58, std::error_code> from_string(std::string_view text) noexcept;

    /**
     * Serialize the value to a base58 encoded string.
     * @return  The base58 encoded string.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    data_chunk value_;
};

} // namespace kth::infrastructure::config

#endif
