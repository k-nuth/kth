// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_SETTINGS_HPP
#define KTH_NODE_SV2_JD_SETTINGS_HPP

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

// The optional seventh list field. It marks an upstream the operator runs,
// which alone is sent downstream worker names.
inline constexpr std::string_view jd_own_upstream = "own";

// Six fields separated by '|', then optionally jd_own_upstream as a seventh;
// an identity containing that delimiter cannot be represented by a list
// entry. Legacy single-pool identities are unchanged.
[[nodiscard]]
std::optional<sv2_settings::jd_upstream> parse_jd_upstream(std::string_view text);

// A host parse_jd_endpoint returned names this machine: localhost, ::1 or
// any 127.x.x.x, judged on the string. Other names are not resolved.
[[nodiscard]]
bool jd_local_host(std::string_view host);

// Downstream worker names go only to the operator's own upstream: one marked
// as such, or one whose configured Mining address is on this machine.
[[nodiscard]]
bool jd_sends_worker_names(sv2_settings::jd_upstream const& upstream);

// Called after validation. An empty list uses the legacy single-pool settings.
[[nodiscard]]
std::vector<sv2_settings::jd_upstream> jd_upstreams(sv2_settings::jd_settings const& settings);

// Check before starting either upstream. The listener separately validates the
// solo payout against its network, even when an upstream will supply outputs.
[[nodiscard]]
bool validate_jd_settings(sv2_settings const& settings, std::string& error);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_SETTINGS_HPP
