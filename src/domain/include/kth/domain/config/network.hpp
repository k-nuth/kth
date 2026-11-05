// Copyright (c) 2016-present Knuth Project developers.
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
    , tempnet
#endif
};

enum class chain_name_format {
    rpc,
    display,
};

/// Network identifier (RPC by default) or human-readable display name.
[[nodiscard]] inline constexpr
std::string_view chain_name(network net, chain_name_format format = chain_name_format::rpc) noexcept {
    auto const display = format == chain_name_format::display;
    switch (net) {
        case network::testnet:
            return display ? "Testnet" : "test";
        case network::regtest:
            return display ? "Regtest" : "regtest";
#if defined(KTH_CURRENCY_BCH)
        case network::testnet4:
            return display ? "Testnet4" : "test4";
        case network::scalenet:
            return display ? "Scalenet" : "scale";
        case network::chipnet:
            return display ? "Chipnet" : "chip";
        case network::tempnet:
            return display ? "Tempnet" : "temp";
#endif
        default:
        case network::mainnet:
            return display ? "Mainnet" : "main";
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
        [](unsigned char c) { return char(std::toupper(c)); });

    if (upper == "MAINNET") return network::mainnet;
    if (upper == "TESTNET") return network::testnet;
    if (upper == "REGTEST") return network::regtest;
#if defined(KTH_CURRENCY_BCH)
    if (upper == "TESTNET4") return network::testnet4;
    if (upper == "SCALENET") return network::scalenet;
    if (upper == "CHIPNET")  return network::chipnet;
    if (upper == "TEMPNET")  return network::tempnet;
#endif
    return std::unexpected(kth::error::illegal_value);
}

} // namespace kth::domain::config

#endif // KTH_DOMAIN_CONFIG_NETWORK_HPP_
