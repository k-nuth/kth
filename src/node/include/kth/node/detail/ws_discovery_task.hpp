// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#ifndef KTH_NODE_DETAIL_WS_DISCOVERY_TASK_HPP
#define KTH_NODE_DETAIL_WS_DISCOVERY_TASK_HPP

#include <kth/network/peer_session.hpp>
#include <kth/network/ws_discovery.hpp>
#include <variant>

namespace kth::node::detail {
struct ws_query {
    network::peer_session::ptr peer;
    network::ws_discovery::request request;
};
struct ws_candidates { std::vector<network::ws_discovery::endpoint> endpoints; };
using ws_discovery_command = std::variant<ws_query, ws_candidates>;
struct ws_discovery_inbox {
    select_channel<ws_discovery_command> commands{64};
};

inline data_chunk ws_packet(uint32_t magic, std::string_view command, data_chunk payload) {
    domain::message::heading heading(magic, std::string(command),
        static_cast<uint32_t>(payload.size()), bitcoin_checksum(payload));
    auto bytes = heading.to_data();
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
}
#endif
