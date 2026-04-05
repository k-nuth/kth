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

// Disk magic for block files (blk*.dat) - matches BCHN for compatibility
// These may differ from network magic to maintain compatibility with historical data
namespace diskmagic {
#if defined(KTH_CURRENCY_LTC)
constexpr uint32_t ltc_mainnet = 0xdbb6c0fbU;
constexpr uint32_t ltc_testnet = 0xf1c8d2fdU;
constexpr uint32_t ltc_regtest = 0xdab5bffaU;
#elif defined(KTH_CURRENCY_BCH)
// BCH uses BTC's disk magic for mainnet/testnet/regtest for historical compatibility
constexpr uint32_t bch_mainnet  = 0xd9b4bef9U;  // Same as BTC mainnet
constexpr uint32_t bch_testnet  = 0x0709110bU;  // Same as BTC testnet
constexpr uint32_t bch_regtest  = 0xdab5bffaU;
constexpr uint32_t bch_testnet4 = 0x92a722cdU;
constexpr uint32_t bch_scalenet = 0xc42dc2baU;
constexpr uint32_t bch_chipnet  = 0x92a722cdU;
#else
constexpr uint32_t btc_mainnet = 0xd9b4bef9U;
constexpr uint32_t btc_testnet = 0x0709110bU;
constexpr uint32_t btc_regtest = 0xdab5bffaU;
#endif
} // namespace diskmagic

/// Get disk magic for a given network type
constexpr uint32_t get_disk_magic(domain::config::network net) {
    switch (net) {
#if defined(KTH_CURRENCY_LTC)
        case domain::config::network::mainnet: return diskmagic::ltc_mainnet;
        case domain::config::network::testnet: return diskmagic::ltc_testnet;
        case domain::config::network::regtest: return diskmagic::ltc_regtest;
#elif defined(KTH_CURRENCY_BCH)
        case domain::config::network::mainnet: return diskmagic::bch_mainnet;
        case domain::config::network::testnet: return diskmagic::bch_testnet;
        case domain::config::network::regtest: return diskmagic::bch_regtest;
        case domain::config::network::testnet4: return diskmagic::bch_testnet4;
        case domain::config::network::scalenet: return diskmagic::bch_scalenet;
        case domain::config::network::chipnet: return diskmagic::bch_chipnet;
#else
        case domain::config::network::mainnet: return diskmagic::btc_mainnet;
        case domain::config::network::testnet: return diskmagic::btc_testnet;
        case domain::config::network::regtest: return diskmagic::btc_regtest;
#endif
        default: return diskmagic::bch_mainnet;
    }
}

namespace config {

enum class currency {
    bitcoin,
    bitcoin_cash,
    litecoin
};

} // namespace config

config::currency get_currency();
domain::config::network get_network(uint32_t identifier, bool is_chipnet);

#if defined(KTH_CURRENCY_BCH)
std::string cashaddr_prefix();
void set_cashaddr_prefix(std::string const& x);
#endif  //KTH_CURRENCY_BCH

} // namespace kth

#endif // KTH_DOMAIN_MULTI_CRYPTO_SUPPORT_HPP
