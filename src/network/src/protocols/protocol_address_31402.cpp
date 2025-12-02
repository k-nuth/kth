// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_address_31402.hpp>

#include <functional>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol.hpp>
#include <kth/network/protocols/protocol_events.hpp>
namespace kth::network {

#define NAME "address"
#define CLASS protocol_address_31402

using namespace kd::message;
using namespace std::placeholders;

static
domain::message::address configured_self(network::settings const& settings) {
    if (settings.self.port() == 0) {
        return {};
    }
    return address{{settings.self.to_network_address()}};
}

protocol_address_31402::protocol_address_31402(p2p& network, channel::ptr channel)
    : protocol_events(network, channel, NAME)
    , network_(network)
    , self_(configured_self(network_.network_settings()))
    , CONSTRUCT_TRACK(protocol_address_31402) {}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_address_31402::start() {
    auto const& settings = network_.network_settings();

    // Must have a handler to capture a shared self pointer in stop subscriber.
    protocol_events::start(BIND1(handle_stop, _1));

    if ( ! self_.addresses().empty()) {
        SEND2(self_, handle_send, _1, self_.command);
    }

    // If we can't store addresses we don't ask for or handle them.
    if (settings.host_pool_capacity == 0) {
        return;
    }

    SUBSCRIBE2(address, handle_receive_address, _1, _2);
    SUBSCRIBE2(get_address, handle_receive_get_address, _1, _2);
    SEND2(get_address{}, handle_send, _1, get_address::command);
}

// Protocol.
// ----------------------------------------------------------------------------

bool protocol_address_31402::handle_receive_address(code const& ec, address_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    spdlog::debug("[network] Storing addresses from [{}] ({})", authority(), message->addresses().size());

    // TODO: manage timestamps (active channels are connected < 3 hours ago).
    network_.store(message->addresses(), BIND1(handle_store_addresses, _1));

    // RESUBSCRIBE
    return true;
}

bool protocol_address_31402::handle_receive_get_address(code const& ec, get_address_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    kd::message::network_address::list addresses;
    network_.fetch_addresses(addresses);

    if ( ! addresses.empty()) {
        address const address_subset(addresses);
        SEND2(address_subset, handle_send, _1, self_.command);

        spdlog::debug("[network] Sending addresses to [{}] ({})", authority(), self_.addresses().size());
    }

    // do not resubscribe; one response per connection permitted
    return false;
}

void protocol_address_31402::handle_store_addresses(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::error("[network] Failure storing addresses from [{}] {}", authority(), ec.message());
        stop(ec);
    }
}

void protocol_address_31402::handle_stop(code const&) {
    // None of the other kth::network protocols log their stop.
    ////spdlog::debug("[network]
    ////] Stopped address protocol for [{}].", authority());
}

} // namespace kth::network
