// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MULTI_CRYPTO_SUPPORT_HPP
#define KTH_DOMAIN_MULTI_CRYPTO_SUPPORT_HPP

#include <cstdint>
#include <string>

#include <kth/domain/config/network.hpp>

namespace kth {

namespace netmagic {
#if defined(KTH_CURRENCY_LTC)
constexpr uint32_t ltc_mainnet = 0xdbb6c0fbU;
constexpr uint32_t ltc_testnet = 0xf1c8d2fdU;
constexpr uint32_t ltc_regtest = 0xdab5bffaU;
#elif defined(KTH_CURRENCY_BCH)
constexpr uint32_t bch_mainnet  = 0xe8f3e1e3U;
constexpr uint32_t bch_testnet  = 0xf4f3e5f4U;
constexpr uint32_t bch_regtest  = 0xfabfb5daU;
constexpr uint32_t bch_testnet4 = 0xafdab7e2U;
constexpr uint32_t bch_scalenet = 0xa2e1afc3U;
constexpr uint32_t bch_chipnet  = 0xafdab7e2U;
#else
constexpr uint32_t btc_mainnet = 0xd9b4bef9U;
constexpr uint32_t btc_testnet = 0x0709110bU;
constexpr uint32_t btc_regtest = 0xdab5bffaU;
#endif
} // namespace netmagic

namespace config {

enum class currency {
    bitcoin,
    bitcoin_cash,
    litecoin
};

} // namespace config

config::currency get_currency();
domain::config::network get_network(uint32_t identifier, bool is_chipnet);

} // namespace kth

#endif // KTH_DOMAIN_MULTI_CRYPTO_SUPPORT_HPP
