// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol.hpp>

#include <cstdint>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/p2p.hpp>

namespace kth::network {

#define NAME "protocol"

protocol::protocol(p2p& network, channel::ptr channel, std::string const& name)
    : pool_(network.thread_pool())
    , dispatch_(network.thread_pool(), NAME)
    , channel_(channel)
    , name_(name) {}

infrastructure::config::authority protocol::authority() const {
    return channel_->authority();
}

std::string const& protocol::name() const {
    return name_;
}

uint64_t protocol::nonce() const {
    return channel_->nonce();
}

version_const_ptr protocol::peer_version() const {
    return channel_->peer_version();
}

void protocol::set_peer_version(version_const_ptr value) {
    channel_->set_peer_version(value);
}

uint32_t protocol::negotiated_version() const {
    return channel_->negotiated_version();
}

void protocol::set_negotiated_version(uint32_t value) {
    channel_->set_negotiated_version(value);
}

threadpool& protocol::pool() {
    return pool_;
}

// Stop the channel.
void protocol::stop(code const& ec) {
    channel_->stop(ec);
}

// protected
void protocol::handle_send(code const& ec, std::string const& command) {
    // std::println("{}", command);
    // Send and receive failures are logged by the proxy.
    // This provides a convenient location for override if desired.
}

} // namespace kth::network
