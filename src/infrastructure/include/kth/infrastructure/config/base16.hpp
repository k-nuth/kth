// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE16_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE16_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base16 encoded data.
 */
//TODO(fernando): make a generic class for baseX
struct KI_API base16 {
    base16() = default;

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
        : value_(value.begin(), value.end()) 
    {}

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    // implicit
    [[nodiscard]] operator data_chunk const&() const noexcept;

    /**
     * Overload cast to generic data reference.
     * @return  This object's value cast to a generic data.
     */
    [[nodiscard]] explicit
    operator data_slice() const noexcept;

    /**
     * Get the underlying data.
     * @return  Reference to the internal data.
     */
    [[nodiscard]]
    data_chunk const& data() const noexcept;

    /**
     * Get the underlying data as a slice.
     * @return  The internal data as a slice.
     */
    [[nodiscard]]
    data_slice as_slice() const noexcept;

    /**
     * Parse a base16 string into a base16 object.
     * @param[in]  text  The base16 encoded string to parse.
     * @return           The parsed base16 object or an error.
     */
    [[nodiscard]] static
    std::expected<base16, std::error_code> from_string(std::string_view text) noexcept;

    /**
     * Serialize the value to a base16 encoded string.
     * @return  The base16 encoded string.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    data_chunk value_;
};

} // namespace kth::infrastructure::config

#endif
