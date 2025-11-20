// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_EVENTS_HPP
#define KTH_NETWORK_PROTOCOL_EVENTS_HPP

#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/protocols/protocol.hpp>

namespace kth::network {

class p2p;

/**
 * Base class for stateful protocol implementation, thread and lock safe.
 */
class KN_API protocol_events
  : public protocol
{
protected:

    /**
     * Construct a protocol instance.
     * @param[in]  network   The network interface.
     * @param[in]  channel   The channel on which to start the protocol.
     * @param[in]  name      The instance name for logging purposes.
     */
    protocol_events(p2p& network, channel::ptr channel,
        std::string const& name);

    /**
     * Start the protocol with no event handler.
     */
    virtual void start();

    /**
     * Start the protocol.
     * The event handler may be invoked one or more times.
     * @param[in]  handler  The handler to call at each completion event.
     */
    virtual void start(event_handler handler);

    /**
     * Invoke the event handler.
     * @param[in]  ec  The error code of the preceding operation.
     */
    virtual void set_event(code const& ec);

    /**
     * Determine if the event handler has been cleared.
     */
    virtual bool stopped() const;

    /**
     * Determine if the code is a stop code or the handler has been cleared.
     */
    virtual bool stopped(code const& ec) const;

private:
    void handle_started(completion_handler handler);
    void handle_stopped(code const& ec);
    void do_set_event(code const& ec);

    kth::atomic<event_handler> handler_;
};

} // namespace kth::network

#endif
