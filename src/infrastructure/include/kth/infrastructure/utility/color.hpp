// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COLOR_HPP
#define KTH_INFRASTRUCTURE_COLOR_HPP

#include <cstdint>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>

namespace kth {

struct KI_API color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

} // namespace kth

#endif
