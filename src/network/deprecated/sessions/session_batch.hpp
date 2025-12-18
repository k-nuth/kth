// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// ============================================================================
// DEPRECATED - This file is scheduled for removal
// ============================================================================
// This file is part of the legacy P2P implementation that is being replaced
// by modern C++23 coroutines and Asio. See doc/asio.md for migration details.
//
// DO NOT USE THIS FILE IN NEW CODE.
// Replacement: Use p2p_node.hpp, peer_session.hpp, protocols_coro.hpp
// ============================================================================

#ifndef KTH_NETWORK_SESSION_BATCH_HPP
#define KTH_NETWORK_SESSION_BATCH_HPP

#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/define.hpp>
#include <kth/network/sessions/session.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/// Intermediate base class for adding batch connect sequence.
struct KN_API session_batch : session {
protected:
    /// Construct an instance.
    session_batch(p2p& network, bool notify_on_connect);

    /// Create a channel from the configured number of concurrent attempts.
    virtual void connect(channel_handler handler);

private:
    // Connect sequence
    void new_connect(channel_handler handler);
    void start_connect(code const& ec, authority const& host, channel_handler handler);
    void handle_connect(code const& ec, channel::ptr channel, connector::ptr connector, channel_handler handler);

    size_t const batch_size_;
};

} // namespace kth::network

#endif
