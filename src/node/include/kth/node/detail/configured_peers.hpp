// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_CONFIGURED_PEERS_HPP
#define KTH_NODE_DETAIL_CONFIGURED_PEERS_HPP

#include <stop_token>
#include <kth/network/peer_session.hpp>
#include <kth/node/define.hpp>

namespace kth::node::detail {

// Report an attempt's exceptions as failures before racing the node's stop.
// Otherwise wait_for_one_success would hide a thrown attempt behind that wait.
KN_API awaitable_expected<network::peer_session::ptr> run_configured_attempt(
    awaitable_expected<network::peer_session::ptr> operation, std::stop_token stop);

} // namespace kth::node::detail

#endif
