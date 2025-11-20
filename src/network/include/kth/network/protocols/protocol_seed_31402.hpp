// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_SEED_31402_HPP
#define KTH_NETWORK_PROTOCOL_SEED_31402_HPP

#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_timer.hpp>

namespace kth::network {

class p2p;

/**
 * Seeding protocol.
 * Attach this to a channel immediately following seed handshake completion.
 */
class KN_API protocol_seed_31402
    : public protocol_timer, track<protocol_seed_31402>
{
public:
    using ptr = std::shared_ptr<protocol_seed_31402>;

    /**
     * Construct a seed protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     */
    protocol_seed_31402(p2p& network, channel::ptr channel);

    /**
     * Start the protocol.
     * @param[in]  handler   Invoked upon stop or complete.
     */
    virtual void start(event_handler handler);

protected:
    virtual void send_own_address(settings const& settings);

    virtual void handle_send_address(code const& ec);
    virtual void handle_send_get_address(code const& ec);
    virtual void handle_store_addresses(code const& ec);
    virtual void handle_seeding_complete(code const& ec, event_handler handler);

    virtual bool handle_receive_address(code const& ec, address_const_ptr address);
    //virtual bool handle_receive_get_address(code const& ec, get_address_const_ptr message);

    p2p& network_;
    const infrastructure::config::authority self_;
};

} // namespace kth::network

#endif
