// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// TEST-ONLY. Not installed, not compiled into any library.
//
// Driving header_download_task with real peer_session objects and no network.
//
// Extracted verbatim from test/header_stall_after_empty.cpp, where it was
// written for the #692 controls. The tip-confirmation controls (#705) need
// exactly the same thing: peers whose answers are decided in advance, and a
// count of how many times each was actually asked.
//
// Everything here is `inline` or lives in an anonymous namespace, so including
// it from two translation units is not an ODR problem.

#ifndef KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP
#define KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP

#include <test_helpers.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <kth/domain/message/headers.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>
#include <kth/node/sync/header_tasks.hpp>

namespace kth::test {
namespace {

using namespace kth::node::sync;

using peer_ptr = network::peer_session::ptr;

// Long enough that the task's re-ask never fires inside these cases: every one
// of them is about what the task does on its own, in one drain, and a re-ask
// landing mid-case would be a second driver the assertions do not account for.
// The cases that ARE about the re-ask live in header_tip_confirmation.cpp and
// pass an interval that fires.
constexpr auto no_recheck = std::chrono::hours(1);

// A peer over an unconnected socket. peer_session tolerates this: it resolves
// the authority defensively (any() when the endpoint is unavailable) and send()
// serializes onto an internal channel rather than the socket. Nothing in this
// control needs the socket, so nothing connects one.
peer_ptr make_peer(::asio::io_context& ioc, network::settings const& config, uint64_t nonce) {
    ::asio::ip::tcp::socket socket(ioc);
    auto peer = std::make_shared<network::peer_session>(std::move(socket), config, false);
    peer->set_nonce(nonce);
    return peer;
}

// The payload of a `headers` message carrying `count` headers, as it would
// arrive on the wire. The dispatcher hands the task exactly this: heading
// stripped, body intact.
data_chunk headers_payload(uint32_t version, size_t count) {
    domain::message::header::list elements;
    elements.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        elements.emplace_back(
            /*version*/ 1u,
            /*previous_block_hash*/ null_hash,
            /*merkle*/ null_hash,
            /*timestamp*/ uint32_t(1231006505u + i),
            /*bits*/ 0x1d00ffffu,
            /*nonce*/ uint32_t(i));
    }

    auto message = domain::message::headers::create(std::move(elements));
    REQUIRE(message.has_value());

    data_chunk out(message->serialized_size(version));
    byte_writer writer(out);
    auto const written = message->to_data(writer, version);
    REQUIRE(written.has_value());
    REQUIRE(writer.position() == out.size());
    return out;
}

// Make a peer fail the exchange it was chosen for. Closing its response
// channel is the one failure that is instantaneous and needs no network: the
// peer is not stopped, so it is eligible and gets selected, send() succeeds,
// and the receive that follows completes with a channel error. The 10s timeout
// inside request_headers_from() reaches the same branch by the slow road; this
// control does not wait it out, and does not claim to test it.
void fail_on_response(peer_ptr const& peer) {
    peer->headers_responses().close();
}

// How many queued answers the peer still holds. Every peer that can be asked
// is loaded with two, so this counts requests by subtraction: two left means
// never asked, one means asked once, zero means asked twice. A boolean
// "was it touched" cannot tell the last two apart -- a second request that is
// still suspended on the channel looks identical to no second request at all,
// which is exactly the hole this closes. Draining is destructive, so call it
// once per peer and last.
size_t answers_left(peer_ptr const& peer) {
    size_t count = 0;
    while (peer->headers_responses().try_receive(
               [](std::error_code, network::raw_message) {})) {
        ++count;
    }
    return count;
}

// Queue a response so it is already waiting when the task asks for it. The
// channel holds ten, so this never blocks and never needs the peer running.
void preload_headers_response(peer_ptr const& peer, size_t count) {
    network::raw_message response;
    response.payload = headers_payload(peer->negotiated_version(), count);
    REQUIRE(peer->headers_responses().try_send(std::error_code{}, std::move(response)));
}

// Two answers, so an unwanted second request is visible as a missing one.
void preload_two(peer_ptr const& peer, size_t count) {
    preload_headers_response(peer, count);
    preload_headers_response(peer, count);
}

// Run until the task can make no further progress. Every operation in this
// control is ready or immediately-ready, so the queue draining IS the task
// having finished everything it intends to do -- there is no timer to outlast
// and no I/O to wait on. A defect that parks the request drains just as fast;
// it simply drains with nothing on the output channel.
void drain(::asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        // keep going while handlers are still becoming ready
    }
}

// Hand one answer to a peer that the task is already waiting on. Used to
// resume a walk that was deliberately suspended mid-flight.
void deliver_headers_response(peer_ptr const& peer, size_t count) {
    preload_headers_response(peer, count);
}

struct outcome {
    bool produced_headers = false;
    peer_ptr source;
    size_t header_count = 0;
    bool reported_failure = false;
    peer_ptr failed_peer;
};

// Read whatever the task published, without waiting for anything it did not.
outcome collect(header_download_output_channel& output) {
    outcome result;
    while (true) {
        std::optional<header_download_output> message;
        bool const got = output.try_receive([&](std::error_code, header_download_output value) {
            message = std::move(value);
        });
        if ( ! got || ! message) {
            break;
        }
        if (auto* downloaded = std::get_if<downloaded_headers>(&*message)) {
            result.produced_headers = true;
            result.source = downloaded->source_peer;
            result.header_count = downloaded->headers.size();
        } else if (auto* failure = std::get_if<peer_failure_report>(&*message)) {
            result.reported_failure = true;
            result.failed_peer = failure->peer;
        }
    }
    return result;
}


} // namespace
} // namespace kth::test

#endif // KTH_NODE_TEST_HEADER_PEER_HARNESS_HPP
