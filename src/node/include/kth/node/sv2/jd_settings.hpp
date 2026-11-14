// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_SETTINGS_HPP
#define KTH_NODE_SV2_JD_SETTINGS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <kth/node/settings.hpp>

namespace kth::node::sv2 {

struct jd_endpoint {
    std::string host;
    uint16_t port;
};

// Authority keys have their own settings, so endpoints cannot carry a URL,
// credentials or an authority in a path. The returned host has no brackets.
[[nodiscard]]
std::optional<jd_endpoint> parse_jd_endpoint(std::string_view text);

// Check before starting either upstream. The listener separately validates the
// solo payout against its network, even when an upstream will supply outputs.
[[nodiscard]]
bool validate_jd_settings(sv2_settings const& settings, std::string& error);

// Whether a host is a Tor onion service name or an I2P name.
[[nodiscard]] bool jd_onion_host(std::string_view host);
[[nodiscard]] bool jd_i2p_host(std::string_view host);

// An .onion upstream can only be reached through net.proxy, and an .i2p one
// through net.i2psam. Checked at startup with the network settings, so a
// node does not start to mine solo because of an address it cannot dial.
[[nodiscard]]
bool validate_jd_route(sv2_settings const& settings, std::string const& proxy,
    std::string const& i2p_bridge, std::string& error);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_SETTINGS_HPP
