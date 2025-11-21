// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/protocols/protocol_header_sync.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/utility/header_list.hpp>

namespace kth::node {

#define NAME "header_sync"
#define CLASS protocol_header_sync

using namespace kth::domain::config;
using namespace kth::domain::message;
using namespace kth::network;
using namespace std::placeholders;

// The interval in which header download rate is measured and tested.
static const asio::seconds expiry_interval(5);

// This class requires protocol version 31800.
protocol_header_sync::protocol_header_sync(full_node& network, channel::ptr channel, header_list::ptr headers, uint32_t minimum_rate)
    : protocol_timer(network, channel, true, NAME)
    , headers_(headers)

    // TODO: replace rate backoff with peer competition.
    //=========================================================================
    , current_second_(0)
    , minimum_rate_(minimum_rate)
    , start_size_(headers->previous_height() - headers->first_height())
    //=========================================================================
    , CONSTRUCT_TRACK(protocol_header_sync)
{}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_header_sync::start(event_handler handler) {
    auto const complete = synchronize<event_handler>(BIND2(headers_complete, _1, handler), 1, NAME);

    protocol_timer::start(expiry_interval, BIND2(handle_event, _1, complete));

    SUBSCRIBE3(headers, handle_receive_headers, _1, _2, complete);

    // This is the end of the start sequence.
    send_get_headers(complete);
}

// Header sync sequence.
// ----------------------------------------------------------------------------

void protocol_header_sync::send_get_headers(event_handler complete) {
    if (stopped()) {
        return;
    }

    get_headers const request {
        {headers_->previous_hash()},
        headers_->stop_hash()
    };

    spdlog::info("[node] protocol_header_sync::send_get_headers [{}]", authority());
    SEND2(request, handle_send, _1, request.command);
}

bool protocol_header_sync::handle_receive_headers(code const& ec, headers_const_ptr message, event_handler complete) {
    if (stopped(ec)) {
        return false;
    }

    auto const start = headers_->previous_height() + 1;

    // A merge failure resets the headers list.
    if ( ! headers_->merge(message)) {
        spdlog::warn("[node] Failure merging headers from [{}]", authority());
        complete(error::invalid_previous_block);
        return false;
    }

    auto const end = headers_->previous_height();

    spdlog::info("[node] Synced headers {}-{} from [{}]", start, end, authority());

    if (headers_->complete()) {
        complete(error::success);
        return false;
    }

    // If we received fewer than 2000 the peer is exhausted, try another.
    if (message->elements().size() < max_get_headers) {
        complete(error::operation_failed);
        return false;
    }

    // This peer has more headers.
    send_get_headers(complete);
    return true;
}

// This is fired by the base timer and stop handler.
void protocol_header_sync::handle_event(code const& ec, event_handler complete) {
    if (stopped(ec)) {
        return;
    }

    if (ec && ec != error::channel_timeout) {
        spdlog::warn("[node] Failure in header sync timer for [{}] {}", authority(), ec.message());
        complete(ec);
        return;
    }

    // TODO: replace rate backoff with peer competition.
    //=========================================================================
    // It was a timeout so another expiry period has passed (overflow ok here).
    current_second_ += static_cast<size_t>(expiry_interval.count());
    auto rate = (headers_->previous_height() - start_size_) / current_second_;
    //=========================================================================

    // Drop the channel if it falls below the min sync rate averaged over all.
    if (rate < minimum_rate_) {
        spdlog::debug("[node] Header sync rate ({}/sec) from [{}]", rate, authority());
        complete(error::channel_timeout);
        return;
    }
}

void protocol_header_sync::headers_complete(code const& ec, event_handler handler) {
    // This is end of the header sync sequence.
    handler(ec);

    // The session does not need to handle the stop.
    stop(error::channel_stopped);
}

} // namespace kth::node