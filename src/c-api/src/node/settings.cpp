// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node/settings.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include <kth/capi/node.h>
#include <kth/capi/helpers.hpp>

#include <kth/domain/multi_crypto_support.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/network/p2p.hpp>
#endif

// ---------------------------------------------------------------------------
extern "C" {

kth_currency_t kth_node_settings_get_currency() {
    return static_cast<kth_currency_t>(int(kth::get_currency()));
}

#if ! defined(__EMSCRIPTEN__)
kth_network_t kth_node_settings_get_network(kth_node_t exec) {

    kth_p2p_t p2p_node = kth_node_get_p2p(exec);
    auto const& node = *static_cast<kth::network::p2p*>(p2p_node);

    auto const& sett = node.network_settings();
    auto id = sett.identifier;
    bool is_chipnet = sett.inbound_port == 48333;

    return static_cast<kth_network_t>(int(kth::get_network(id, is_chipnet)));
}
#endif


} // extern "C"
