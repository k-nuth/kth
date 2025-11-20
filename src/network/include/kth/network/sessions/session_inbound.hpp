// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SESSION_INBOUND_HPP
#define KTH_NETWORK_SESSION_INBOUND_HPP

#include <cstddef>
#include <memory>
#include <vector>
#include <kth/domain.hpp>
#include <kth/network/acceptor.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/sessions/session.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/// Inbound connections session, thread safe.
class KN_API session_inbound : public session, track<session_inbound> {
public:
    using ptr = std::shared_ptr<session_inbound>;

    /// Construct an instance.
    session_inbound(p2p& network, bool notify_on_connect);

    /// Start the session.
    void start(result_handler handler) override;

protected:
    /// Overridden to implement pending test for inbound channels.
    void handshake_complete(channel::ptr channel, result_handler handle_started) override;

    /// Override to attach specialized protocols upon channel start.
    virtual void attach_protocols(channel::ptr channel);

private:
    void start_accept(code const& ec);

    void handle_stop(code const& ec);
    void handle_started(code const& ec, result_handler handler);
    void handle_accept(code const& ec, channel::ptr channel);

    void handle_channel_start(code const& ec, channel::ptr channel);
    void handle_channel_stop(code const& ec);

    // These are thread safe.
    acceptor::ptr acceptor_;
    size_t const connection_limit_;
};

} // namespace kth::network

#endif
