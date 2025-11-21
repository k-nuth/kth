// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_ping_60001.hpp>

#include <cstdint>
#include <functional>
#include <string>

#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_timer.hpp>

namespace kth::network {

#define CLASS protocol_ping_60001

using namespace kd::message;
using namespace std::placeholders;

protocol_ping_60001::protocol_ping_60001(p2p& network, channel::ptr channel)
    : protocol_ping_31402(network, channel)
    , pending_(false)
    , CONSTRUCT_TRACK(protocol_ping_60001) {}

// This is fired by the callback (i.e. base timer and stop handler).
void protocol_ping_60001::send_ping(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    if (ec && ec != error::channel_timeout) {
        spdlog::debug("[network] Failure in ping timer for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    if (pending_) {
        spdlog::debug("[network] Ping latency limit exceeded [{}]", authority());
        stop(error::channel_timeout);
        return;
    }

    pending_ = true;
    auto const nonce = pseudo_random_broken_do_not_use::next();
    SUBSCRIBE3(pong, handle_receive_pong, _1, _2, nonce);
    SEND2(ping{ nonce }, handle_send_ping, _1, ping::command);
}

void protocol_ping_60001::handle_send_ping(code const& ec, const std::string&) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::debug("[network] Failure sending ping to [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }
}

bool protocol_ping_60001::handle_receive_ping(code const& ec, ping_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure getting ping from [{}] {}", authority(), ec.message());
        stop(ec);
        return false;
    }

    SEND2(pong{ message->nonce() }, handle_send, _1, pong::command);
    return true;
}

bool protocol_ping_60001::handle_receive_pong(code const& ec, pong_const_ptr message, uint64_t nonce) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure getting pong from [{}] {}", authority(), ec.message());
        stop(ec);
        return false;
    }

    pending_ = false;

    if (message->nonce() != nonce) {
        spdlog::warn("[network] Invalid pong nonce from [{}]", authority());
        stop(error::bad_stream);
        return false;
    }

    return false;
}

} // namespace kth::network