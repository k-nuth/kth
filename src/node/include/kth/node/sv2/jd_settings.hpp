// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_SETTINGS_HPP
#define KTH_NODE_SV2_JD_SETTINGS_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// Six fields separated by '|'; an identity containing that delimiter cannot
// be represented by a list entry. Legacy single-pool identities are unchanged.
[[nodiscard]]
std::optional<sv2_settings::jd_upstream> parse_jd_upstream(std::string_view text);

// Called after validation. An empty list uses the legacy single-pool settings.
[[nodiscard]]
std::vector<sv2_settings::jd_upstream> jd_upstreams(sv2_settings::jd_settings const& settings);

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

// The longest an upstream may take to answer before it is failed as slow:
// sv2.jd.max_latency_milliseconds, and not less than what the route to it
// needs. An answer through Tor or over I2P crosses several relays each way,
// and the limit that suits a direct connection would fail a healthy upstream
// there at every slow circuit. The floors stand in the same relation to the
// default limit as the dial allowances of jd_route do to a direct dial.
inline constexpr std::chrono::milliseconds jd_proxy_latency_floor = std::chrono::seconds(15);
inline constexpr std::chrono::milliseconds jd_i2p_latency_floor = std::chrono::seconds(45);

// `host` is the upstream's, and `through_proxy` whether net.proxy is set: an
// I2P host is reached over I2P, and any other through the proxy when there
// is one.
[[nodiscard]]
std::chrono::milliseconds jd_latency_limit(sv2_settings::jd_settings const& settings, std::string_view host,
    bool through_proxy);

// Whether a Reconnect may move a connection from `present_host` to
// `target_host`. An upstream reached over I2P does not learn the node's
// address; with no proxy, an address outside I2P is dialed directly, so a
// Reconnect from the one to the other is not followed.
[[nodiscard]]
bool jd_reconnect_keeps_route(std::string_view present_host, std::string_view target_host, bool through_proxy);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_SETTINGS_HPP
