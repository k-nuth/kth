// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONFIG_NETWORK_HPP_
#define KTH_DOMAIN_CONFIG_NETWORK_HPP_

#include <algorithm>
#include <cctype>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>

namespace kth::domain::config {

// For configuration network initialization in other libraries.
enum class network {
     mainnet
    , testnet
    , regtest
#if defined(KTH_CURRENCY_BCH)
    , testnet4
    , scalenet
    , chipnet
#endif
};

inline constexpr
std::string_view name(network net) {
    switch (net) {
        case network::testnet:
            return "Testnet";
        case network::regtest:
            return "Regtest";
#if defined(KTH_CURRENCY_BCH)
        case network::testnet4:
            return "Testnet4";
        case network::scalenet:
            return "Scalenet";
        case network::chipnet:
            return "Chipnet";
#endif
        default:
        case network::mainnet:
            return "Mainnet";
    }
}

/**
 * Parse a case-insensitive network name. Returns `error::illegal_value`
 * on an unknown value.
 */
[[nodiscard]] inline
std::expected<network, kth::code> parse_network(std::string_view text) {
    std::string upper;
    upper.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(upper),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper == "MAINNET") return network::mainnet;
    if (upper == "TESTNET") return network::testnet;
    if (upper == "REGTEST") return network::regtest;
#if defined(KTH_CURRENCY_BCH)
    if (upper == "TESTNET4") return network::testnet4;
    if (upper == "SCALENET") return network::scalenet;
    if (upper == "CHIPNET")  return network::chipnet;
#endif
    return std::unexpected(kth::error::illegal_value);
}

} // namespace kth::domain::config

#endif // KTH_DOMAIN_CONFIG_NETWORK_HPP_
