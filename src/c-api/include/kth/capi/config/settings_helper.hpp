// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CONFIG_SETTINGS_HELPERS_HPP_
#define KTH_CAPI_CONFIG_SETTINGS_HELPERS_HPP_

#include <vector>

#include <kth/capi/config/blockchain_helpers.hpp>
#include <kth/capi/config/database_helpers.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/capi/config/network_helpers.hpp>
#endif

#include <kth/capi/config/node_helpers.hpp>
#include <kth/capi/config/helpers.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/conversions/config.hpp>
#include <kth/node/configuration.hpp>

namespace kth::capi::helpers {

inline
kth::node::configuration settings_to_cpp(kth_settings const& x) {
    auto const net = kth::network_to_cpp(x.network.context);
    kth::node::configuration config(net);
    config.node = node_settings_to_cpp(x.node);
    config.chain = blockchain_settings_to_cpp(x.chain, net);
    config.database = database_settings_to_cpp(x.database);
#if ! defined(__EMSCRIPTEN__)
    config.network = network_settings_to_cpp(x.network);
#endif
    return config;
}

// inline
// kth_node_settings settings_to_c(kth::node::settings const& x) {
//     return node_settings_to_common<kth_node_settings>(x);
// }

} // namespace kth::capi::helpers

#endif // KTH_CAPI_CONFIG_SETTINGS_HELPERS_HPP_
