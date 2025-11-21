// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_ping_31402.hpp>

#include <functional>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_timer.hpp>

namespace kth::network {

#define NAME "ping"
#define CLASS protocol_ping_31402

using namespace kd::message;
using namespace std::placeholders;

protocol_ping_31402::protocol_ping_31402(p2p& network, channel::ptr channel)
    : protocol_timer(network, channel, true, NAME)
    , settings_(network.network_settings())
    , CONSTRUCT_TRACK(protocol_ping_31402) {}

void protocol_ping_31402::start() {
    protocol_timer::start(settings_.channel_heartbeat(), BIND1(send_ping, _1));

    SUBSCRIBE2(ping, handle_receive_ping, _1, _2);

    // Send initial ping message by simulating first heartbeat.
    set_event(error::success);
}

// This is fired by the callback (i.e. base timer and stop handler).
void protocol_ping_31402::send_ping(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    if (ec && ec != error::channel_timeout) {
        spdlog::debug("[network] Failure in ping timer for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    SEND2(ping{}, handle_send, _1, ping::command);
}

bool protocol_ping_31402::handle_receive_ping(code const& ec, ping_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    // RESUBSCRIBE
    return true;
}

} // namespace kth::network
