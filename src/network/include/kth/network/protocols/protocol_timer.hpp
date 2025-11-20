// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_TIMER_HPP
#define KTH_NETWORK_PROTOCOL_TIMER_HPP

#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol_events.hpp>

namespace kth::network {

class p2p;

/**
 * Base class for timed protocol implementation.
 */
class KN_API protocol_timer
  : public protocol_events
{
protected:

    /**
     * Construct a timed protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel    The channel on which to start the protocol.
     * @param[in]  perpetual  Set for automatic timer reset unless stopped.
     * @param[in]  name       The instance name for logging purposes.
     */
    protocol_timer(p2p& network, channel::ptr channel, bool perpetual,
        std::string const& name);

    /**
     * Define the event handler and start the protocol and timer.
     * The timer is automatically canceled on stop (only).
     * The timer is suspended while the handler is executing.
     * @param[in]  timeout  The timer period (not automatically reset).
     * @param[in]  handler  Invoke automatically on stop and timer events.
     */
    virtual void start(const asio::duration& timeout, event_handler handler);

protected:
    void reset_timer();

private:
    void handle_timer(code const& ec);
    void handle_notify(code const& ec, event_handler handler);

    bool const perpetual_;
    deadline::ptr timer_;
};

} // namespace kth::network

#endif
