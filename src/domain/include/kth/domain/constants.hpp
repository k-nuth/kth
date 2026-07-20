// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_HPP_
#define KTH_DOMAIN_CONSTANTS_HPP_

#include <kth/infrastructure/assumptions.hpp>
#include <kth/infrastructure/constants.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <kth/domain/constants/bch.hpp>
#elif defined(KTH_CURRENCY_BTC)
#include <kth/domain/constants/btc.hpp>
#elif defined(KTH_CURRENCY_LTC)
#include <kth/domain/constants/ltc.hpp>
#else
#error Unsupported currency
#endif

#include <kth/domain/constants/functions.hpp>

namespace kth {

#if defined(KTH_CURRENCY_BCH)
namespace internal_max_block_sigchecks {
constexpr size_t mainnet = static_max_block_size(domain::config::network::mainnet) / block_maxbytes_maxsigchecks_ratio;
constexpr size_t testnet4 = static_max_block_size(domain::config::network::testnet4) / block_maxbytes_maxsigchecks_ratio;
constexpr size_t scalenet = static_max_block_size(domain::config::network::scalenet) / block_maxbytes_maxsigchecks_ratio;
constexpr size_t chipnet = static_max_block_size(domain::config::network::chipnet) / block_maxbytes_maxsigchecks_ratio;
} // namespace internal_max_block_sigchecks

constexpr inline
size_t static_max_block_sigchecks(domain::config::network network) noexcept {
    if (network == domain::config::network::testnet4) return internal_max_block_sigchecks::testnet4;
    if (network == domain::config::network::scalenet) return internal_max_block_sigchecks::scalenet;
    if (network == domain::config::network::chipnet) return internal_max_block_sigchecks::chipnet;
    return internal_max_block_sigchecks::mainnet;
}

#endif

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_HPP_
