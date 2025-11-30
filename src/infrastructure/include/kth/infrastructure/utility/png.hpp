// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PNG_HPP
#define KTH_INFRASTRUCTURE_PNG_HPP

#include <cstdint>
#include <ostream>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/color.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <png.h>

namespace kth {

struct KI_API png {
    static constexpr uint32_t margin = 2;
    static constexpr uint32_t dots_per_inch = 72;
    static constexpr uint32_t inches_per_meter = (100.0 / 2.54);

    static 
    color get_default_foreground() {
        static constexpr color default_foreground{0, 0, 0, 255};
        return default_foreground;
    }

    static 
    color get_default_background() {
        static constexpr color default_background{255, 255, 255, 255};
        return default_background;
    }

    static
    bool write_png(byte_span data, uint32_t size, std::ostream& out);

    static
    bool write_png(byte_span data, uint32_t size,
        uint32_t dots_per_inch, uint32_t margin, uint32_t inches_per_meter,
        color const& foreground, color const& background, std::ostream& out);
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_PNG_HPP
