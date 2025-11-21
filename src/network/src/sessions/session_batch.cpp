// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sessions/session_batch.hpp>

#include <cstddef>

#include <kth/domain.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/sessions/session.hpp>

namespace kth::network {

#define CLASS session_batch
#define NAME "session_batch"

using namespace kth::config;
using namespace kd::message;
using namespace std::placeholders;

session_batch::session_batch(p2p& network, bool notify_on_connect)
    : session(network, notify_on_connect)
    , batch_size_(std::max(settings_.connect_batch_size, 1u)) {}

// Connect sequence.
// ----------------------------------------------------------------------------

// protected:
void session_batch::connect(channel_handler handler) {
    auto const join_handler = synchronize(handler, batch_size_, NAME "_join", synchronizer_terminate::on_success);

    for (size_t host = 0; host < batch_size_; ++host) {
        new_connect(join_handler);
    }
}

void session_batch::new_connect(channel_handler handler) {
    if (stopped()) {
        spdlog::debug("[network] Suspended batch connection.");
        handler(error::channel_stopped, nullptr);
        return;
    }

    network_address address;
    auto const ec = fetch_address(address);
    start_connect(ec, address, handler);
}

void session_batch::start_connect(code const& ec, authority const& host, channel_handler handler) {
    if (stopped(ec)) {
        spdlog::debug("[network] Batch session stopped while starting.");
        handler(error::service_stopped, nullptr);
        return;
    }

    // This termination prevents a tight loop in the empty address pool case.
    if (ec) {
        spdlog::warn("[network] Failure fetching new address: {}", ec.message());
        handler(ec, nullptr);
        return;
    }

    // This creates a tight loop in the case of a small address pool.
    if (blacklisted(host)) {
        spdlog::debug("[network] Fetched blacklisted address [{}] ", host);
        handler(error::address_blocked, nullptr);
        return;
    }

    spdlog::debug("[network] Connecting to [{}]", host);

    auto const connector = create_connector();
    pend(connector);

    // CONNECT
    connector->connect(host, BIND4(handle_connect, _1, _2, connector, handler));
}

void session_batch::handle_connect(code const& ec, channel::ptr channel, connector::ptr connector, channel_handler handler) {
    unpend(connector);

    if (ec) {
        handler(ec, nullptr);
        return;
    }

    spdlog::debug("[network] Connected to [{}]", channel->authority());

    // This is the end of the connect sequence.
    handler(error::success, channel);
}

} // namespace kth::network