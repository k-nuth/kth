// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SESSION_SEED_HPP
#define KTH_NETWORK_SESSION_SEED_HPP

#include <cstddef>
#include <memory>
#include <vector>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/define.hpp>
#include <kth/network/sessions/session.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/// Seed connections session, thread safe.
class KN_API session_seed : public session, track<session_seed> {
public:
    using ptr = std::shared_ptr<session_seed>;

    /// Construct an instance.
    session_seed(p2p& network);

    /// Start the session.
    void start(result_handler handler) override;

protected:
    /// Overridden to set service and version mins upon session start.
    void attach_handshake_protocols(channel::ptr channel, result_handler handle_started) override;

    /// Override to attach specialized protocols upon channel start.
    virtual void attach_protocols(channel::ptr channel, result_handler handler);

private:
    void start_seeding(size_t start_size, result_handler handler);
    void start_seed(infrastructure::config::endpoint const& seed, result_handler handler);
    void handle_started(code const& ec, result_handler handler);
    void handle_connect(code const& ec, channel::ptr channel, infrastructure::config::endpoint const& seed, connector::ptr connector, result_handler handler);
    void handle_complete(size_t start_size, result_handler handler);
    void handle_channel_start(code const& ec, channel::ptr channel, result_handler handler);
    void handle_channel_stop(code const& ec);
};

} // namespace kth::network

#endif

