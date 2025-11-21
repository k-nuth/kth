// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SESSION_MANUAL_HPP
#define KTH_NETWORK_SESSION_MANUAL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/define.hpp>
#include <kth/network/sessions/session.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

class p2p;

/// Manual connections session, thread safe.
struct KN_API session_manual : session, track<session_manual> {
public:
    using ptr = std::shared_ptr<session_manual>;
    using channel_handler = std::function<void(code const&, channel::ptr)>;

    /// Construct an instance.
    session_manual(p2p& network, bool notify_on_connect);

    /// Start the manual session.
    void start(result_handler handler) override;

    /// Maintain connection to a node.
    virtual void connect(std::string const& hostname, uint16_t port);

    /// Maintain connection to a node with callback on first connect.
    virtual void connect(std::string const& hostname, uint16_t port, channel_handler handler);

protected:
    /// Override to attach specialized protocols upon channel start.
    virtual void attach_protocols(channel::ptr channel);

private:
    void start_connect(code const& ec, std::string const& hostname, uint16_t port, uint32_t attempts, channel_handler handler);
    void handle_started(code const& ec, result_handler handler);
    void handle_connect(code const& ec, channel::ptr channel, std::string const& hostname, uint16_t port, uint32_t remaining, connector::ptr connector, channel_handler handler);
    void handle_channel_start(code const& ec, std::string const& hostname, uint16_t port, channel::ptr channel, channel_handler handler);
    void handle_channel_stop(code const& ec, std::string const& hostname, uint16_t port);
};

} // namespace kth::network

#endif
