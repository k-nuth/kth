// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_CONFIG_BASE2_HPP
#define KTH_INFRASTUCTURE_CONFIG_BASE2_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

/**
 * Serialization helper for base2 encoded data.
 */
struct KI_API base2 {
    base2() = default;

    /**
     * @param[in]  value  The value to initialize with.
     */
    explicit
    base2(binary const& value);

    /**
     * Get number of bits in value.
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * Overload cast to internal type.
     * @return  This object's value cast to internal type reference.
     */
    [[nodiscard]] explicit
    operator binary const&() const noexcept;

    /**
     * Parse a base2 string into a base2 object.
     * @param[in]  text  The base2 encoded string to parse.
     * @return           The parsed base2 object or an error.
     */
    [[nodiscard]] static
    std::expected<base2, std::error_code> from_string(std::string_view text) noexcept;

    /**
     * Serialize the value to a base2 encoded string.
     * @return  The base2 encoded string.
     */
    [[nodiscard]]
    std::string to_string() const;

private:
    binary value_;
};

} // namespace kth::infrastructure::config

#endif
