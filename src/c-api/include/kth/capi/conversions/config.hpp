// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Network selection and currency-dependent switches (kth/domain/config).

#ifndef KTH_CAPI_CONVERSIONS_CONFIG_HPP_
#define KTH_CAPI_CONVERSIONS_CONFIG_HPP_

#include <type_traits>

#include <kth/capi/detail/enum_cast.hpp>
#include <kth/capi/detail/handle_access.hpp>

#include <kth/capi/config/network.h>

#include <kth/domain/config/network.hpp>

namespace kth {

// Out-of-range C values map to `mainnet`.
inline constexpr kth::domain::config::network network_to_cpp(kth_network_t x) noexcept {
    switch (x) {
        case kth_network_testnet:
            return kth::domain::config::network::testnet;
        case kth_network_regtest:
            return kth::domain::config::network::regtest;
#if defined(KTH_CURRENCY_BCH)
        case kth_network_testnet4:
            return kth::domain::config::network::testnet4;
        case kth_network_scalenet:
            return kth::domain::config::network::scalenet;
        case kth_network_chipnet:
            return kth::domain::config::network::chipnet;
#endif
        default:
        case kth_network_mainnet:
            return kth::domain::config::network::mainnet;
    }
}

inline constexpr kth_network_t network_to_c(kth::domain::config::network x) noexcept {
    switch (x) {
        case kth::domain::config::network::testnet:
            return kth_network_testnet;
        case kth::domain::config::network::regtest:
            return kth_network_regtest;
#if defined(KTH_CURRENCY_BCH)
        case kth::domain::config::network::testnet4:
            return kth_network_testnet4;
        case kth::domain::config::network::scalenet:
            return kth_network_scalenet;
        case kth::domain::config::network::chipnet:
            return kth_network_chipnet;
#endif
        default:
        case kth::domain::config::network::mainnet:
            return kth_network_mainnet;
    }
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::config::network>) <= sizeof(kth_network_t));
static_assert(network_to_c(kth::domain::config::network::mainnet) == kth_network_mainnet);
static_assert(network_to_cpp(kth_network_mainnet) == kth::domain::config::network::mainnet);
static_assert(network_to_c(kth::domain::config::network::testnet) == kth_network_testnet);
static_assert(network_to_cpp(kth_network_testnet) == kth::domain::config::network::testnet);
static_assert(network_to_c(kth::domain::config::network::regtest) == kth_network_regtest);
static_assert(network_to_cpp(kth_network_regtest) == kth::domain::config::network::regtest);
#if defined(KTH_CURRENCY_BCH)
static_assert(network_to_c(kth::domain::config::network::testnet4) == kth_network_testnet4);
static_assert(network_to_cpp(kth_network_testnet4) == kth::domain::config::network::testnet4);
static_assert(network_to_c(kth::domain::config::network::scalenet) == kth_network_scalenet);
static_assert(network_to_cpp(kth_network_scalenet) == kth::domain::config::network::scalenet);
static_assert(network_to_c(kth::domain::config::network::chipnet) == kth_network_chipnet);
static_assert(network_to_cpp(kth_network_chipnet) == kth::domain::config::network::chipnet);
#endif

// Segregated witness is not available when building for BCH.
inline bool witness(int x = 1) noexcept {
#if defined(KTH_CURRENCY_BCH)
    return false;
#else
    return kth::int_to_bool(x);
#endif
}

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_CONFIG_HPP_ */
