// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Chain enums (kth/domain/chain).

#ifndef KTH_CAPI_CONVERSIONS_CHAIN_HPP_
#define KTH_CAPI_CONVERSIONS_CHAIN_HPP_

#include <kth/capi/detail/enum_cast.hpp>

#include <kth/capi/chain/endorsement_type.h>
#include <kth/capi/chain/point_kind.h>
#include <kth/capi/chain/token_capability.h>

#include <kth/domain/chain/history.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/token_data.hpp>

namespace kth {

// Out-of-range C values map to `output`.
inline constexpr kth::domain::chain::point_kind point_kind_to_cpp(kth_point_kind_t x) {
    switch (x) {
        case kth_point_kind_spend:
            return kth::domain::chain::point_kind::spend;
        default:
        case kth_point_kind_output:
            return kth::domain::chain::point_kind::output;
    }
}

inline constexpr kth_point_kind_t point_kind_to_c(kth::domain::chain::point_kind x) {
    return ecast<kth_point_kind_t>(x);
}

// Out-of-range C values map to `none`.
inline constexpr kth::domain::chain::capability_t token_capability_to_cpp(kth_token_capability_t x) {
    switch (x) {
        case kth_token_capability_mut:
            return kth::domain::chain::capability_t::mut;
        case kth_token_capability_minting:
            return kth::domain::chain::capability_t::minting;
        default:
        case kth_token_capability_none:
            return kth::domain::chain::capability_t::none;
    }
}

inline constexpr kth_token_capability_t token_capability_to_c(kth::domain::chain::capability_t x) {
    return ecast<kth_token_capability_t>(x);
}

// Out-of-range C values map to `ecdsa`.
inline constexpr kth::domain::chain::endorsement_type endorsement_type_to_cpp(kth_endorsement_type_t x) {
    switch (x) {
        case kth_endorsement_type_schnorr:
            return kth::domain::chain::endorsement_type::schnorr;
        default:
        case kth_endorsement_type_ecdsa:
            return kth::domain::chain::endorsement_type::ecdsa;
    }
}

inline constexpr kth_endorsement_type_t endorsement_type_to_c(kth::domain::chain::endorsement_type x) {
    return ecast<kth_endorsement_type_t>(x);
}

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_CHAIN_HPP_ */
