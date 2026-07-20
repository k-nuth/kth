// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_TRISTATE_HPP_
#define KTH_INFRASTRUCTURE_UTILITY_TRISTATE_HPP_

#include <cstdint>

namespace kth {

enum class tristate : uint8_t {
    no = 0,
    yes = 1,
    unknown = 2
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_UTILITY_TRISTATE_HPP_
