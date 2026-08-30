// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
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

#include "header_peer_harness.hpp"

using namespace kth;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// An empty header response must drive the next request, not park it (#692)
// =============================================================================
//
// A peer that answers `getheaders` with zero headers is saying "I have nothing
// past your locator", not "the chain ends here". The download task already knows
// this: it marks the peer as at its tip, asks for the next eligible peer, and —
// when it finds one — keeps the request buffered in `pending_request`.
//
// What it does NOT do is send it. Control returns to the task's main loop, which
// blocks on `input.async_receive()`. Nothing in the task re-examines
// `pending_request`; only an inbound `peers_updated` or a fresh `header_request`
// does, and neither is on a timer. With a stable peer set at the header tip the
// node therefore waits for an unrelated peer to connect or drop before it asks
// the peer it already chose.
//
// That is not a slow path, it is an unbounded one: the delay is whatever the
// network's next churn event happens to cost. Three BCH mainnet runs measured
// 23m25s, 33m47s and (live, still parked when observed) 16m43s+ between the
// empty response and the request that followed it.
//
// The control below is the same sequence with the network removed. Both peers
// are real `peer_session` objects over unconnected sockets — `send()` only
// enqueues onto an internal channel and never touches the socket, and their
// response channels are pre-loaded, so the whole exchange is a handful of
// channel operations with no I/O and no waiting. Peer A answers empty, peer B
// has a header to give. The task is asked for headers once. If it drives its own
// pending request, B's header reaches the output channel; if it parks it, the
// output channel stays empty and there is no second event coming to save it.



// -----------------------------------------------------------------------------

TEST_CASE("header download: an empty response drives the next eligible peer", "[header_stall]") {
    ::asio::io_context ioc;
    network::settings config;

    auto empty_peer = make_peer(ioc, config, 1001);
    auto next_peer = make_peer(ioc, config, 1002);
    auto spare_peer = make_peer(ioc, config, 1003);

    // A has nothing past the locator; B has one header to give. C is there to
    // be left alone -- the walk must stop at the peer that answered. Two
    // answers each, so the count that survives says how many times each was
    // asked.
    preload_two(empty_peer, 0);
    preload_two(next_peer, 1);
    preload_two(spare_peer, 1);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{empty_peer, next_peer, spare_peer}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash}));

    drain(ioc);

    auto const result = collect(output);

    // B was asked, and answered. This is the assertion the defect fails: on the
    // parked-request code the task stops after A and nothing reaches the output
    // channel.
    CHECK(result.produced_headers);
    CHECK(result.header_count == 1u);
    CHECK(result.source == next_peer);

    // The empty answer is not a peer failure and must not be reported as one.
    CHECK_FALSE(result.reported_failure);

    // Exactly one request each to A and B -- one answer consumed, one left.
    // Two left would mean the peer was never asked (and would have made the
    // checks above vacuous); zero would mean it was asked twice.
    CHECK(answers_left(empty_peer) == 1u);
    CHECK(answers_left(next_peer) == 1u);

    // C keeps both: B satisfied the request, so the walk stopped before it.
    CHECK(answers_left(spare_peer) == 2u);
}

TEST_CASE("header download: a peer that goes away between the answer and the retry is skipped", "[header_stall]") {
    ::asio::io_context ioc;
    network::settings config;

    auto empty_peer = make_peer(ioc, config, 2001);
    auto stopped_peer = make_peer(ioc, config, 2002);
    auto live_peer = make_peer(ioc, config, 2003);

    preload_two(empty_peer, 0);
    preload_two(live_peer, 1);

    // The peer that would be chosen next is already gone by the time the empty
    // answer arrives. Driving the pending request must skip it rather than
    // spend the request on a session that cannot answer.
    stopped_peer->stop();

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{empty_peer, stopped_peer, live_peer}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash}));

    drain(ioc);

    auto const result = collect(output);

    CHECK(result.produced_headers);
    CHECK(result.source == live_peer);

    // One request each: the stopped peer was skipped without spending one.
    CHECK(answers_left(empty_peer) == 1u);
    CHECK(answers_left(live_peer) == 1u);
}

TEST_CASE("header download: a peer that fails the exchange it was chosen for is reported, not retried here", "[header_stall]") {
    ::asio::io_context ioc;
    network::settings config;

    auto empty_peer = make_peer(ioc, config, 2101);
    auto failing_peer = make_peer(ioc, config, 2102);
    auto spare_peer = make_peer(ioc, config, 2103);

    preload_two(empty_peer, 0);
    fail_on_response(failing_peer);
    preload_two(spare_peer, 1);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{empty_peer, failing_peer, spare_peer}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash}));

    drain(ioc);

    auto const result = collect(output);

    // A answered empty and B -- eligible, selected, and asked -- failed the
    // exchange. That B was genuinely asked is the point of the case: it is not
    // the already-stopped peer above, which never gets selected at all.
    REQUIRE(result.reported_failure);
    CHECK(result.failed_peer == failing_peer);
    CHECK(answers_left(empty_peer) == 1u);

    // This is the second parking, and it is deliberate. A failure is not
    // "ask the next peer": the task marks the peer failed, hands the report
    // out, and waits. What answers is the coordinator, which turns the report
    // into a fresh header_request -- a different driver from the walk above,
    // and one this task cannot see. So C is untouched here, and no headers
    // were produced, even though C had some to give.
    CHECK(answers_left(spare_peer) == 2u);
    CHECK_FALSE(result.produced_headers);
}

TEST_CASE("header download: every peer answering empty settles instead of spinning", "[header_stall]") {
    ::asio::io_context ioc;
    network::settings config;

    auto first = make_peer(ioc, config, 3001);
    auto second = make_peer(ioc, config, 3002);

    // Both are at their tip. Driving the pending request must walk the peer set
    // once and stop, not cycle over peers already known to be at their tip.
    preload_two(first, 0);
    preload_two(second, 0);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{first, second}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash}));

    drain(ioc);

    auto const result = collect(output);

    // Both asked exactly once: the walk does not revisit a peer already in
    // peers_at_tip, so neither loses its second answer.
    CHECK(answers_left(first) == 1u);
    CHECK(answers_left(second) == 1u);

    // Reaching the tip on every peer is the completion signal, not a failure.
    CHECK(result.produced_headers);
    CHECK(result.header_count == 0u);
    CHECK_FALSE(result.reported_failure);
}


TEST_CASE("header download: a walk in flight stops when the task's input closes", "[header_stall]") {
    ::asio::io_context ioc;
    network::settings config;

    auto empty_peer = make_peer(ioc, config, 4001);
    auto slow_peer = make_peer(ioc, config, 4002);
    auto spare_peer = make_peer(ioc, config, 4003);

    // A answers at once. B is deliberately left without an answer, so the walk
    // suspends inside its request and the test gets control back mid-flight --
    // the one point where a shutdown can overlap a walk. C is loaded so that a
    // walk that carries on past B is visible as a spent answer.
    preload_two(empty_peer, 0);
    preload_two(spare_peer, 0);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{empty_peer, slow_peer, spare_peer}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash}));

    drain(ioc);

    // Mid-walk: A has been asked and answered. C is deliberately NOT inspected
    // here -- answers_left drains, so reading it now would empty the very
    // channel the assertion at the end depends on.
    REQUIRE(answers_left(empty_peer) == 1u);

    // Shutdown closes the task's input while the walk is suspended on B. This
    // is the real ordering: the orchestrator cancels and closes these channels
    // before the peer sessions go down, so B can still answer afterwards.
    input.cancel();
    input.close();

    // B answers now. Without the check in drive_request the walk would take
    // this as "ask the next one" and go on to C.
    deliver_headers_response(slow_peer, 0);
    drain(ioc);

    // B really was mid-request: it consumed the answer handed to it just now.
    // Without this the case would pass on a task that never got past A.
    CHECK(answers_left(slow_peer) == 0u);

    // C was never asked: both of its answers are still queued.
    CHECK(answers_left(spare_peer) == 2u);
}
