// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_PING_31402_HPP
#define KTH_NETWORK_PROTOCOL_PING_31402_HPP

#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_timer.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/**
 * Ping-pong protocol.
 * Attach this to a channel immediately following handshake completion.
 */
class KN_API protocol_ping_31402
    : public protocol_timer, track<protocol_ping_31402>
{
public:
    using ptr = std::shared_ptr<protocol_ping_31402>;

    /**
     * Construct a ping protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     */
    protocol_ping_31402(p2p& network, channel::ptr channel);

    /**
     * Start the protocol.
     */
    virtual void start();

protected:
    virtual void send_ping(code const& ec);

    virtual bool handle_receive_ping(code const& ec, ping_const_ptr message);

    settings const& settings_;
};

} // namespace kth::network

#endif
