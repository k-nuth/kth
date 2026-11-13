// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PNG_HPP
#define KTH_INFRASTRUCTURE_PNG_HPP

#include <cstdint>
#include <expected>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/color.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

enum class png_errc {
    invalid_data,
    invalid_scale,
    dimensions_out_of_range,
    invalid_resolution,
    allocation_failed,
    encoding_failed
};

struct KI_API png {
    static constexpr uint32_t margin = 2;
    static constexpr uint32_t dots_per_inch = 72;
    static constexpr uint32_t inches_per_meter = (100.0 / 2.54);

    static color get_default_foreground() { return {0, 0, 0, 255}; }
    static color get_default_background() { return {255, 255, 255, 255}; }

    struct options {
        uint32_t scale = 1;
        uint32_t margin = png::margin;
        uint32_t dots_per_inch = png::dots_per_inch;
        uint32_t inches_per_meter = png::inches_per_meter;
        color foreground = get_default_foreground();
        color background = get_default_background();
    };

    // Encode the version/width-prefixed square bitmap produced by the QR helper.
    // The caller owns the returned bytes. No streams or output parameters.
    [[nodiscard]] static std::expected<data_chunk, png_errc> write(byte_span input, uint32_t scale = 1);
    [[nodiscard]] static std::expected<data_chunk, png_errc> write(byte_span input, options const& settings);
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_PNG_HPP
