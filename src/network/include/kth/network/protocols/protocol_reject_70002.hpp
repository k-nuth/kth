// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_REJECT_70002_HPP
#define KTH_NETWORK_PROTOCOL_REJECT_70002_HPP

#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_events.hpp>

namespace kth::network {

class p2p;

class KN_API protocol_reject_70002
    : public protocol_events, track<protocol_reject_70002>
{
public:
    using ptr = std::shared_ptr<protocol_reject_70002>;

    /**
     * Construct a reject protocol for logging reject payloads.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel for the protocol.
     */
    protocol_reject_70002(p2p& network, channel::ptr channel);

    /**
     * Start the protocol.
     */
    virtual void start();

protected:
    virtual bool handle_receive_reject(code const& ec, reject_const_ptr reject);
};

} // namespace kth::network

#endif

