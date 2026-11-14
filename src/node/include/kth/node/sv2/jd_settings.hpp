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

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_SETTINGS_HPP
