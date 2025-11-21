// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_events.hpp>

#include <functional>
#include <string>

#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol.hpp>

namespace kth::network {

#define CLASS protocol_events

using namespace std::placeholders;

protocol_events::protocol_events(p2p& network, channel::ptr channel, std::string const& name)
    : protocol(network, channel, name) {}

// Properties.
// ----------------------------------------------------------------------------

bool protocol_events::stopped() const {
    // Used for context-free stop testing.
    return !handler_.load();
}

bool protocol_events::stopped(code const& ec) const {
    // The service stop code may also make its way into protocol handlers.
    return stopped() || ec == error::channel_stopped || ec == error::service_stopped;
}

// Start.
// ----------------------------------------------------------------------------

void protocol_events::start() {
    auto const nop = [](code const&){};
    start(nop);
}

void protocol_events::start(event_handler handler) {
    handler_.store(handler);
    SUBSCRIBE_STOP1(handle_stopped, _1);
}

// Stop.
// ----------------------------------------------------------------------------

void protocol_events::handle_stopped(code const& ec) {
    if ( ! stopped(ec)) {
        spdlog::debug("[network] Stop protocol_{} on [{}] {}", name(), authority(), ec.message());
    }

    // Event handlers can depend on this code for channel stop.
    set_event(error::channel_stopped);
}

// Set Event.
// ----------------------------------------------------------------------------

void protocol_events::set_event(code const& ec) {
    // If already stopped.
    auto handler = handler_.load();
    if ( ! handler) {
        return;
    }

    // If stopping but not yet cleared, clear event handler now.
    if (stopped(ec)) {
        handler_.store(nullptr);
    }

    // Invoke event handler.
    handler(ec);
}

} // namespace kth::network
