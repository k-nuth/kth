// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/node/detail/` is excluded from the install rules, so nothing here is part
// of the surface a consumer can reach. `p2p_node` names this type as a friend and
// nothing else; without this header that friendship can be used by no one.

#ifndef KTH_NODE_DETAIL_P2P_NODE_TEST_SEAM_HPP_
#define KTH_NODE_DETAIL_P2P_NODE_TEST_SEAM_HPP_

#include <utility>

#include <kth/node/p2p_node.hpp>

namespace kth::node::detail {

/// Watch the status task at its four points.
///
/// The task is detached, logs at info level and reports to nobody, so "a stop cut
/// this wait short" and "the wait expired on its own" look identical from
/// outside. A control that timed the shutdown instead would be measuring a window
/// that other components also delay — which is how a wall-clock control for the
/// executor's heartbeat passed on Linux and failed on macOS.
struct p2p_node_test_seam {
    using probe_point = p2p_node::status_probe_point;

    template <typename Probe>
    static void set_status_probe(p2p_node& target, Probe probe) {
        target.status_probe_ = std::move(probe);
    }
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_P2P_NODE_TEST_SEAM_HPP_
