// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONSTANTS_FUNCTIONS_HPP_
#define KTH_DOMAIN_CONSTANTS_FUNCTIONS_HPP_

#include <kth/domain/config/network.hpp>
#include <kth/domain/constants/bch.hpp>
#include <kth/domain/constants/bch_btc.hpp>

namespace kth {

constexpr uint32_t work_limit(bool retarget = true) noexcept {
    return retarget ? retarget_proof_of_work_limit : no_retarget_proof_of_work_limit;
}

constexpr inline
size_t static_max_block_size(domain::config::network network) noexcept {
#if defined(KTH_CURRENCY_BCH)
    if (network == domain::config::network::testnet4) return max_block_size::testnet4;
    if (network == domain::config::network::scalenet) return max_block_size::scalenet;
    if (network == domain::config::network::chipnet) return max_block_size::chipnet;
    return max_block_size::mainnet_new;
#else
    return max_block_size;
#endif  //KTH_CURRENCY_BCH
}

constexpr inline
size_t get_max_payload_size(domain::config::network network) noexcept {
#if defined(KTH_CURRENCY_BCH)
    if (network == domain::config::network::scalenet) return max_payload_size_scalenet;
#endif  //KTH_CURRENCY_BCH
    return max_payload_size;
}

constexpr inline
size_t static_max_block_size_network_independent() noexcept {
#if defined(KTH_CURRENCY_BCH)
    return max_block_size::mainnet_new;
#else
    return max_block_size;
#endif  //KTH_CURRENCY_BCH
}

constexpr inline
size_t static_absolute_max_block_size() noexcept {
#if defined(KTH_CURRENCY_BCH)
    return 2'000'000'000;
#else
    return max_block_size;
#endif
}

constexpr inline
size_t static_max_block_sigops(domain::config::network network) noexcept {
#if defined(KTH_CURRENCY_BCH)
    if (network == domain::config::network::testnet4) return max_block_sigops::testnet4;
    if (network == domain::config::network::scalenet) return max_block_sigops::scalenet;
    if (network == domain::config::network::chipnet) return max_block_sigops::chipnet;
    return max_block_sigops::mainnet_new;
#else
    return max_block_sigops;
#endif  //KTH_CURRENCY_BCH
}

constexpr inline
size_t get_allowed_sigops(size_t block_size) noexcept {
    return (1 + ((block_size - 1) / one_million_bytes_block)) * sigops_per_million_bytes;
}

constexpr inline
uint64_t max_money_recursive(uint64_t money) noexcept {
    return money > 0 ? money + max_money_recursive(money >> 1) : 0;
}

constexpr inline
uint64_t bitcoin_to_satoshi(uint64_t bitcoin_uints = 1) noexcept {
    return bitcoin_uints * satoshi_per_bitcoin;
}

constexpr inline
uint64_t initial_block_subsidy_satoshi() noexcept {
    return bitcoin_to_satoshi(initial_block_subsidy_bitcoin);
}

constexpr
uint64_t subsidy_interval(bool retarget = true) noexcept {
    return retarget ? retarget_subsidy_interval : no_retarget_subsidy_interval;
}

constexpr
uint64_t max_money(bool retarget = true) noexcept {
    ////// Optimize out the derivation of recursive_money.
    ////KTH_ASSERT(recursive_money == max_money_recursive(
    ////    initial_block_subsidy_satoshi()));

    return recursive_money * subsidy_interval(retarget);
}

constexpr inline
size_t network_map(domain::config::network network, size_t mainnet, size_t testnet, size_t regtest
#if defined(KTH_CURRENCY_BCH)
, size_t testnet4
, size_t scalenet
, size_t chipnet
#endif
) noexcept {
    switch (network) {
        case domain::config::network::testnet:
            return testnet;
        case domain::config::network::regtest:
            return regtest;
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4:
            return testnet4;
        case domain::config::network::scalenet:
            return scalenet;
        case domain::config::network::chipnet:
            return chipnet;
#endif
        default:
        case domain::config::network::mainnet:
            return mainnet;
    }
}

template <typename T, typename R>
constexpr inline
bool network_relation(domain::config::network network, R r, T const& value, T const& mainnet, T const& testnet, T const& regtest
#if defined(KTH_CURRENCY_BCH)
, T const& testnet4
, T const& scalenet
, T const& chipnet
#endif
) noexcept {
    switch (network) {
        case domain::config::network::testnet:
            return r(value, testnet);
        case domain::config::network::regtest:
            return r(value, regtest);
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4:
            return r(value, testnet4);
        case domain::config::network::scalenet:
            return r(value, scalenet);
        case domain::config::network::chipnet:
            return r(value, chipnet);
#endif
        default:
        case domain::config::network::mainnet:
            return r(value, mainnet);
    }
}

} // namespace kth

#endif // KTH_DOMAIN_CONSTANTS_FUNCTIONS_HPP_
