// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SESSION_OUTBOUND_HPP
#define KTH_NETWORK_SESSION_OUTBOUND_HPP

#include <cstddef>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/sessions/session_batch.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/// Outbound connections session, thread safe.
class KN_API session_outbound : public session_batch, track<session_outbound> {
public:
    using ptr = std::shared_ptr<session_outbound>;

    /// Construct an instance.
    session_outbound(p2p& network, bool notify_on_connect);

    /// Start the session.
    void start(result_handler handler) override;

protected:
    /// Overridden to implement pending outbound channels.
    void start_channel(channel::ptr channel,
        result_handler handle_started) override;

    /// Overridden to attach minimum service level for witness support.
    void attach_handshake_protocols(channel::ptr channel, result_handler handle_started) override;

    /// Override to attach specialized protocols upon channel start.
    virtual void attach_protocols(channel::ptr channel);

private:
    void new_connection(code const&);

    void handle_started(code const& ec, result_handler handler);
    void handle_connect(code const& ec, channel::ptr channel);
    void do_unpend(code const& ec, channel::ptr channel, result_handler handle_started);
    void handle_channel_stop(code const& ec, channel::ptr channel);
    void handle_channel_start(code const& ec, channel::ptr channel);
};

} // namespace kth::network

#endif
