// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Node enums (kth/node).

#ifndef KTH_CAPI_CONVERSIONS_NODE_HPP_
#define KTH_CAPI_CONVERSIONS_NODE_HPP_

#include <kth/capi/detail/enum_cast.hpp>

#include <kth/capi/node/start_modules.h>

#include <kth/node/full_node.hpp>

namespace kth {

// Out-of-range C values map to `all`.
inline constexpr kth::node::start_modules start_modules_to_cpp(kth_start_modules_t x) noexcept {
    switch (x) {
        case kth_start_modules_just_chain:
            return kth::node::start_modules::just_chain;
        case kth_start_modules_just_p2p:
            return kth::node::start_modules::just_p2p;
        default:
        case kth_start_modules_all:
            return kth::node::start_modules::all;
    }
}

inline constexpr kth_start_modules_t start_modules_to_c(kth::node::start_modules x) noexcept {
    return ecast<kth_start_modules_t>(x);
}

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_NODE_HPP_ */
