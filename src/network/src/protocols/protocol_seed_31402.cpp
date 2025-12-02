// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_seed_31402.hpp>

#include <functional>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_timer.hpp>

namespace kth::network {

#define NAME "seed"
#define CLASS protocol_seed_31402

using namespace kd::message;
using namespace std::placeholders;

// Require three callbacks (or any error) before calling complete.
protocol_seed_31402::protocol_seed_31402(p2p& network, channel::ptr channel)
    : protocol_timer(network, channel, false, NAME)
    , network_(network)
    , CONSTRUCT_TRACK(protocol_seed_31402) {}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_seed_31402::start(event_handler handler) {
    auto const& settings = network_.network_settings();
    event_handler const complete = BIND2(handle_seeding_complete, _1, handler);

    if (settings.host_pool_capacity == 0) {
        complete(error::not_found);
        return;
    }

    auto const join_handler = synchronize(complete, 3, NAME, synchronizer_terminate::on_error);

    protocol_timer::start(settings.channel_germination(), join_handler);

    SUBSCRIBE2(address, handle_receive_address, _1, _2);
    send_own_address(settings);
    SEND1(get_address(), handle_send_get_address, _1);
}

// Protocol.
// ----------------------------------------------------------------------------

void protocol_seed_31402::send_own_address(settings const& settings) {
    if (settings.self.port() == 0) {
        set_event(error::success);
        return;
    }

    address const self(network_address::list{network_address{ settings.self.to_network_address() } });

    SEND1(self, handle_send_address, _1);
}

void protocol_seed_31402::handle_seeding_complete(code const& ec, event_handler handler) {
    handler(ec);
    stop(ec);
}

bool protocol_seed_31402::handle_receive_address(code const& ec, address_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    spdlog::debug("[network] Storing addresses from seed [{}] ({})", authority(), message->addresses().size());

    // TODO: manage timestamps (active channels are connected < 3 hours ago).
    network_.store(message->addresses(), BIND1(handle_store_addresses, _1));
    return false;
}

void protocol_seed_31402::handle_send_address(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    // 1 of 3
    set_event(error::success);
}

void protocol_seed_31402::handle_send_get_address(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::debug("[network] Failure sending get_address to seed [{}] {}", authority(), ec.message());
        set_event(ec);
        return;
    }

    // 2 of 3
    set_event(error::success);
}

void protocol_seed_31402::handle_store_addresses(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::error("[network] Failure storing addresses from seed [{}] {}", authority(), ec.message());
        set_event(ec);
        return;
    }

    spdlog::debug("[network] Stopping completed seed [{}] ", authority());

    // 3 of 3
    set_event(error::channel_stopped);
}

} // namespace kth::network