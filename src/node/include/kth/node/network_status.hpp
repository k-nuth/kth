// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_NETWORK_STATUS_HPP
#define KTH_NODE_NETWORK_STATUS_HPP

#include <cstddef>
#include <variant>
#include <kth/node/query_port.hpp>

namespace kth::node {

struct network_status {
    bool active;
    size_t connections;
};

// No query parameters: the P2P owner supplies its current status when served.
using network_status_port = query_port<std::monostate, network_status>;

} // namespace kth::node

#endif // KTH_NODE_NETWORK_STATUS_HPP
