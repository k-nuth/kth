// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_timer.hpp>

#include <functional>
#include <memory>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_events.hpp>

namespace kth::network {

#define CLASS protocol_timer
using namespace std::placeholders;

protocol_timer::protocol_timer(p2p& network, channel::ptr channel, bool perpetual, std::string const& name)
    : protocol_events(network, channel, name)
    , perpetual_(perpetual) {}

// Start sequence.
// ----------------------------------------------------------------------------

// protected:
void protocol_timer::start(const asio::duration& timeout, event_handler handle_event) {
    // The deadline timer is thread safe.
    timer_ = std::make_shared<deadline>(pool(), timeout);
    protocol_events::start(BIND2(handle_notify, _1, handle_event));
    reset_timer();
}

void protocol_timer::handle_notify(code const& ec, event_handler handler) {
    if (ec == error::channel_stopped) {
        timer_->stop();
    }

    handler(ec);
}

// Timer.
// ----------------------------------------------------------------------------

// protected:
void protocol_timer::reset_timer() {
    if (stopped()) {
        return;
    }

    timer_->start(BIND1(handle_timer, _1));
}

void protocol_timer::handle_timer(code const& ec) {
    if (stopped()) {
        return;
    }

    spdlog::debug("[network] Fired protocol_{} timer on [{}] {}", name(), authority(), ec.message());

    // The handler completes before the timer is reset.
    set_event(error::channel_timeout);

    // A perpetual timer resets itself until the channel is stopped.
    if (perpetual_) {
        reset_timer();
    }
}

} // namespace kth::network
