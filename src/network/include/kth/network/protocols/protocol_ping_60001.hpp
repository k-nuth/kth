// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_PING_60001_HPP
#define KTH_NETWORK_PROTOCOL_PING_60001_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_timer.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/**
 * Ping-pong protocol.
 * Attach this to a channel immediately following handshake completion.
 */
class KN_API protocol_ping_60001
  : public protocol_ping_31402, track<protocol_ping_60001>
{
public:
    using ptr = std::shared_ptr<protocol_ping_60001>;

    /**
     * Construct a ping protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     */
    protocol_ping_60001(p2p& network, channel::ptr channel);

protected:
    void send_ping(code const& ec) override;

    void handle_send_ping(code const& ec, std::string const& command);
    bool handle_receive_ping(code const& ec, ping_const_ptr message) override;
    virtual bool handle_receive_pong(code const& ec, pong_const_ptr message, uint64_t nonce);

private:
    std::atomic<bool> pending_;
};

} // namespace kth::network

#endif
