// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Chain enums (kth/domain/chain).

#ifndef KTH_CAPI_CONVERSIONS_CHAIN_HPP_
#define KTH_CAPI_CONVERSIONS_CHAIN_HPP_

#include <type_traits>

#include <kth/capi/detail/enum_cast.hpp>

#include <kth/capi/chain/endorsement_type.h>
#include <kth/capi/chain/point_kind.h>
#include <kth/capi/chain/token_capability.h>

#include <kth/domain/chain/history.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/token_data.hpp>

namespace kth {

// Out-of-range C values map to `output`.
inline constexpr kth::domain::chain::point_kind point_kind_to_cpp(kth_point_kind_t x) noexcept {
    switch (x) {
        case kth_point_kind_spend:
            return kth::domain::chain::point_kind::spend;
        default:
        case kth_point_kind_output:
            return kth::domain::chain::point_kind::output;
    }
}

inline constexpr kth_point_kind_t point_kind_to_c(kth::domain::chain::point_kind x) noexcept {
    return ecast<kth_point_kind_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::chain::point_kind>) <= sizeof(kth_point_kind_t));
static_assert(point_kind_to_c(kth::domain::chain::point_kind::output) == kth_point_kind_output);
static_assert(point_kind_to_cpp(kth_point_kind_output) == kth::domain::chain::point_kind::output);
static_assert(point_kind_to_c(kth::domain::chain::point_kind::spend) == kth_point_kind_spend);
static_assert(point_kind_to_cpp(kth_point_kind_spend) == kth::domain::chain::point_kind::spend);

// Out-of-range C values map to `none`.
inline constexpr kth::domain::chain::capability_t token_capability_to_cpp(kth_token_capability_t x) noexcept {
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

inline constexpr kth_token_capability_t token_capability_to_c(kth::domain::chain::capability_t x) noexcept {
    return ecast<kth_token_capability_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::chain::capability_t>) <= sizeof(kth_token_capability_t));
static_assert(token_capability_to_c(kth::domain::chain::capability_t::none) == kth_token_capability_none);
static_assert(token_capability_to_cpp(kth_token_capability_none) == kth::domain::chain::capability_t::none);
static_assert(token_capability_to_c(kth::domain::chain::capability_t::mut) == kth_token_capability_mut);
static_assert(token_capability_to_cpp(kth_token_capability_mut) == kth::domain::chain::capability_t::mut);
static_assert(token_capability_to_c(kth::domain::chain::capability_t::minting) == kth_token_capability_minting);
static_assert(token_capability_to_cpp(kth_token_capability_minting) == kth::domain::chain::capability_t::minting);

// Out-of-range C values map to `ecdsa`.
inline constexpr kth::domain::chain::endorsement_type endorsement_type_to_cpp(kth_endorsement_type_t x) noexcept {
    switch (x) {
        case kth_endorsement_type_schnorr:
            return kth::domain::chain::endorsement_type::schnorr;
        default:
        case kth_endorsement_type_ecdsa:
            return kth::domain::chain::endorsement_type::ecdsa;
    }
}

inline constexpr kth_endorsement_type_t endorsement_type_to_c(kth::domain::chain::endorsement_type x) noexcept {
    return ecast<kth_endorsement_type_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::chain::endorsement_type>) <= sizeof(kth_endorsement_type_t));
static_assert(endorsement_type_to_c(kth::domain::chain::endorsement_type::ecdsa) == kth_endorsement_type_ecdsa);
static_assert(endorsement_type_to_cpp(kth_endorsement_type_ecdsa) == kth::domain::chain::endorsement_type::ecdsa);
static_assert(endorsement_type_to_c(kth::domain::chain::endorsement_type::schnorr) == kth_endorsement_type_schnorr);
static_assert(endorsement_type_to_cpp(kth_endorsement_type_schnorr) == kth::domain::chain::endorsement_type::schnorr);

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_CHAIN_HPP_ */
