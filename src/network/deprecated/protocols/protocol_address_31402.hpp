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

#ifndef KTH_NETWORK_PROTOCOL_ADDRESS_31402_HPP
#define KTH_NETWORK_PROTOCOL_ADDRESS_31402_HPP

#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_events.hpp>

namespace kth::network {

class p2p;

/**
 * Address protocol.
 * Attach this to a channel immediately following handshake completion.
 */
struct KN_API protocol_address_31402 : protocol_events, track<protocol_address_31402> {
public:
    using ptr = std::shared_ptr<protocol_address_31402>;

    /**
     * Construct an address protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     */
    protocol_address_31402(p2p& network, channel::ptr channel);

    /**
     * Start the protocol.
     */
    virtual void start();

protected:
    virtual void handle_stop(code const& ec);
    virtual void handle_store_addresses(code const& ec);
    virtual bool handle_receive_address(code const& ec, address_const_ptr address);
    virtual bool handle_receive_get_address(code const& ec, get_address_const_ptr message);

    p2p& network_;
    domain::message::address const self_;
};

} // namespace kth::network

#endif
