// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "header_peer_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>

#include <kth/domain/message/version.hpp>
#include <kth/node/sync/header_tasks.hpp>

using namespace kth;
using namespace kth::node::sync;
using namespace kth::test;

// =============================================================================
// Confirming the tip, and never asking the same peer the same thing (#705)
// =============================================================================
//
// A peer can say "I have nothing past your locator" in two ways. It can answer
// with no headers, or it can answer with headers the chain already holds. On
// the wire the second is indistinguishable from progress -- 2000 headers
// arrived, in 157ms -- and only the organizer, two hops downstream, knows that
// none of them was new.
//
// The download task marked a peer spent only for the first way. So a peer that
// answered the second way was handed straight back by sticky selection and
// asked again, at the same height, with the same locator: on BCH mainnet,
// eleven times in 1.43 s. And because the tip is confirmed only when no peer
// has anything left to give, that one peer kept the confirmation out of reach
// for as long as it stayed connected. When it left, every other peer was
// already spent, the eligible set was empty, and the request parked on an event
// that could not arrive: `pending=true`, eight peers connected, no getdata ever
// issued, for as long as the node was left running.
//
// The controls below drive the real task with real peer_session objects over
// unconnected sockets, exactly as the #692 controls do. No network, no timers
// to outlast: every operation is ready, so a drained io_context IS the task
// having done everything it intends to.

namespace {

// A peer that claims a height in its version message. best_known_height is
// built from these, and it is what separates "nobody left to ask, and we are as
// high as anyone claims" from "nobody left to ask, and we are not".
void claim_height(peer_ptr const& peer, uint32_t height) {
    domain::message::version v;
    v.set_start_height(height);
    peer->set_peer_version(std::make_shared<domain::message::version const>(std::move(v)));
}

// Everything the task published, in order. `collect` folds; these cases need
// the sequence, because "confirmed the tip, then took a new header" and "took
// a new header, then confirmed the tip" are different outcomes.
std::vector<size_t> header_batches(header_download_output_channel& output) {
    std::vector<size_t> sizes;
    while (true) {
        std::optional<header_download_output> message;
        bool const got = output.try_receive([&](std::error_code, header_download_output value) {
            message = std::move(value);
        });
        if ( ! got || ! message) break;
        if (auto* downloaded = std::get_if<downloaded_headers>(&*message)) {
            sizes.push_back(downloaded->headers.size());
        }
    }
    return sizes;
}

} // namespace

// -----------------------------------------------------------------------------

TEST_CASE("header tip: a peer whose answer moved nothing is not asked again for that height",
          "[header_tip]") {
    // The defect, at its origin. A answers with headers -- as many as a peer
    // sends when it is at the same tip we are -- and the organizer adds none of
    // them. The coordinator carries that verdict back on the next request. The
    // task must spend A and ask B, not hand A back because the wire looked busy.
    ::asio::io_context ioc;
    network::settings config;

    auto spent = make_peer(ioc, config, 7001);
    auto next = make_peer(ioc, config, 7002);

    preload_two(spent, 2000);
    preload_two(next, 1);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{spent, next}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // A was asked once and answered with 2000 headers. Without this the case
    // would pass on a task that never asked anybody.
    auto const first = collect(output);
    REQUIRE(first.produced_headers);
    REQUIRE(first.source == spent);
    REQUIRE(first.header_count == 2000u);

    // The coordinator's verdict: that batch added nothing, and the peer that
    // sent it is spent. This is the request it sends next.
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, spent->nonce()}));
    drain(ioc);

    auto const second = collect(output);

    // B was asked, and answered.
    CHECK(second.produced_headers);
    CHECK(second.source == next);

    // A was asked exactly once in the whole case -- one answer consumed, one
    // left. Zero left is the defect: it was asked again for the same height.
    CHECK(answers_left(spent) == 1u);
    CHECK(answers_left(next) == 1u);
}

TEST_CASE("header tip: every eligible peer answering without progress confirms the tip",
          "[header_tip]") {
    // Both peers have nothing new, by the two different ways of saying it: A
    // answers with known headers and is spent by the coordinator's verdict, B
    // answers empty and is spent by the task itself. With nobody left to ask,
    // at a height at or past what any peer claims, that IS the answer.
    ::asio::io_context ioc;
    network::settings config;

    auto known = make_peer(ioc, config, 7101);
    auto empty = make_peer(ioc, config, 7102);
    claim_height(known, 965489u);
    claim_height(empty, 965489u);

    preload_two(known, 2000);
    preload_two(empty, 0);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{known, empty}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);
    REQUIRE(collect(output).source == known);

    // A is spent. The walk goes to B, which answers empty and is spent too;
    // with both gone the tip is confirmed in the same drain.
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, known->nonce()}));
    drain(ioc);

    auto const result = collect(output);

    // The confirmation is an empty batch, which is what the coordinator turns
    // into "header sync complete" and the start of block sync.
    CHECK(result.produced_headers);
    CHECK(result.header_count == 0u);

    // Not a failure. Being current is not a peer misbehaving, and reporting it
    // as one is what produced the retry storm.
    CHECK_FALSE(result.reported_failure);

    // Each asked exactly once across the whole case.
    CHECK(answers_left(known) == 1u);
    CHECK(answers_left(empty) == 1u);
}

TEST_CASE("header tip: peers in the list that cannot be asked are not eligible ones",
          "[header_tip]") {
    // Two peers in the task's list, neither able to answer: one spent for this
    // walk, one retired — and retiring ends the session, so that one is not
    // "connected" any more either, only still present in a list that has not
    // been refreshed. That is exactly the state the log used to describe as a
    // contradiction: it printed the size of the list and then refused to select
    // from it, in the same millisecond, because those are two different sets
    // sharing a word. The observable difference is that the task must reach a
    // decision here rather than wait for a list that is already as good as it
    // will get.
    ::asio::io_context ioc;
    network::settings config;

    auto spent = make_peer(ioc, config, 7201);
    auto excluded = make_peer(ioc, config, 7202);
    claim_height(spent, 965489u);
    claim_height(excluded, 965489u);

    preload_two(spent, 2000);
    preload_two(excluded, 1);

    // Excluded by retirement rather than by stopping. Both are exclusions of a
    // connected peer, which is what this case is about, but stopping a session
    // now empties its response channel — correctly, since nobody will ever read
    // it (#706) — and that channel is this case's evidence that the peer was
    // never asked. Stopped peers are excluded too, and the #692 controls cover
    // that.
    excluded->retire_from_header_requests();

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{spent, excluded}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, spent->nonce()}));
    drain(ioc);

    auto const result = collect(output);

    // Two peers connected, zero eligible, and the task said so by confirming
    // rather than parking.
    CHECK(result.produced_headers);
    CHECK(result.header_count == 0u);

    // Neither was asked: one was spent before the request, the other cannot be
    // selected. Both keep both answers.
    // The spent peer was not asked and keeps both answers. The excluded one is
    // excluded by having been ended — retiring a session ends it, so the slot
    // is released and the manager replaces it (#706) — and a walk that selected
    // it would have reported a failure rather than an answer, which is what
    // makes this more than "nothing happened".
    CHECK(answers_left(spent) == 2u);
    CHECK(excluded->stopped());
    CHECK_FALSE(result.reported_failure);
}

TEST_CASE("header tip: a session retired after a timeout is not asked again",
          "[header_tip]") {
    // The other half of the retirement rule (#706). A session that gave up
    // without consuming its answer may still have a `headers` in flight that
    // nothing can recognise, so it stops being a header source: asking it again
    // is what would let that message answer the next request.
    ::asio::io_context ioc;
    network::settings config;

    auto retired = make_peer(ioc, config, 7901);
    auto usable = make_peer(ioc, config, 7902);
    claim_height(retired, 965489u);
    claim_height(usable, 965489u);

    preload_two(retired, 1);
    preload_two(usable, 1);

    // As request_headers does on timeout.
    retired->retire_from_header_requests();

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{retired, usable}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    auto const result = collect(output);
    REQUIRE(result.produced_headers);

    // The usable peer answered; the retired one was never selected, so it keeps
    // both of its answers.
    CHECK(result.source == usable);
    CHECK(answers_left(usable) == 1u);

    // The retired one was never selected. Its own channel cannot say so any
    // more — retiring ends the session, which closes it — but a walk that had
    // chosen it would have failed its send and reported that instead.
    CHECK(retired->stopped());
    CHECK_FALSE(result.reported_failure);
}

TEST_CASE("header tip: with nobody to ask, the tip is not confirmed",
          "[header_tip]") {
    // The tip is confirmed by evidence from peers, never by their absence. With
    // no peers at all, best_known_height is 0 because nobody claims otherwise,
    // so every height is trivially "at or past" it -- and a task that read its
    // own empty eligible set as agreement would confirm the tip at startup,
    // before a single peer had answered, and start block sync over whatever the
    // chain happened to hold.
    ::asio::io_context ioc;

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    // No peers_updated at all: the task has never seen a peer.
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // Nothing confirmed, nothing claimed. The request waits for a peer, which
    // is the honest state and an event that can actually happen.
    CHECK(header_batches(output).empty());
}

TEST_CASE("header tip: peers that were never asked do not confirm the tip either",
          "[header_tip]") {
    // The same rule with peers present but unusable. Nobody has answered, so
    // there is no evidence, and an empty eligible set is not agreement.
    ::asio::io_context ioc;
    network::settings config;

    auto gone = make_peer(ioc, config, 7601);
    claim_height(gone, 965489u);
    preload_two(gone, 0);

    // Retired rather than stopped, for the same reason as above: stopping now
    // drains the channel this case reads as evidence.
    gone->retire_from_header_requests();

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{gone}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    CHECK(header_batches(output).empty());

    // It was never selected: a stopped session fails its send at once, so a
    // walk that had chosen it would have produced a failure report rather than
    // silence.
    CHECK(gone->stopped());
    auto const result = collect(output);
    CHECK_FALSE(result.reported_failure);
}

TEST_CASE("header tip: a confirmation waits for room rather than being dropped",
          "[header_tip]") {
    // The confirmation is the only message that tells the coordinator the tip
    // was reached, and the only thing that starts block sync. Sent with
    // try_send it is lost whenever the output channel happens to be full, and
    // losing it puts the node back exactly where this fix found it: at the tip,
    // nothing downloading bodies, and nothing left to re-drive it.
    ::asio::io_context ioc;
    network::settings config;

    auto only = make_peer(ioc, config, 7701);
    claim_height(only, 965489u);
    preload_two(only, 0);

    header_download_input_channel input(ioc, 16);

    // Room for exactly one message, and it is taken before the walk starts.
    header_download_output_channel output(ioc, 1);
    REQUIRE(output.try_send(std::error_code{}, downloaded_headers{
        .headers = {}, .start_height = 1u, .source_peer = nullptr}));

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{only}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // The peer was asked and answered, so the walk reached the confirmation --
    // which is now waiting for room rather than having been thrown away.
    CHECK(answers_left(only) == 1u);

    // Take the occupant out. Nothing else frees this channel.
    std::optional<header_download_output> occupant;
    REQUIRE(output.try_receive([&](std::error_code, header_download_output value) {
        occupant = std::move(value);
    }));
    drain(ioc);

    // And the confirmation lands. On try_send it never arrives, and the only
    // trace is a warning nobody acts on.
    auto const batches = header_batches(output);
    CHECK(batches == std::vector<size_t>{0u});
}

TEST_CASE("header tip: headers wait for room rather than being dropped",
          "[header_tip]") {
    // The third thing this task sends, and the last one that was still dropped
    // on a full channel. A drop leaves the request parked with no event behind
    // it, so the coordinator goes on believing a walk is running — and an
    // announcement coalesced against that belief is never reconsidered.
    // Draining this channel does not wake the task either: it reads from its
    // input, and nothing turns a drop here into an inbound message.
    ::asio::io_context ioc;
    network::settings config;

    auto peer = make_peer(ioc, config, 7851);
    preload_two(peer, 3);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 1);
    REQUIRE(output.try_send(std::error_code{}, downloaded_headers{
        .headers = {}, .start_height = 1u, .source_peer = nullptr}));

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{peer}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // The peer was asked and answered, so the walk reached the delivery and is
    // waiting there rather than having thrown the headers away.
    CHECK(answers_left(peer) == 1u);

    std::optional<header_download_output> occupant;
    REQUIRE(output.try_receive([&](std::error_code, header_download_output value) {
        occupant = std::move(value);
    }));
    drain(ioc);

    // And they land. On try_send they never arrive, and the only trace is a
    // warning nobody acts on.
    CHECK(header_batches(output) == std::vector<size_t>{3u});
}

TEST_CASE("header tip: the max-height completion also waits for room",
          "[header_tip]") {
    // The second way this task can say "sync is finished": a configured ceiling
    // rather than the peers running out. It answers before a peer is ever
    // chosen, so it never reaches the tip path, and it used to send with
    // try_send — dropping it on a full channel is a sync that never completes,
    // exactly as it was for the tip.
    ::asio::io_context ioc;
    network::settings config;

    auto spare = make_peer(ioc, config, 7801);
    preload_two(spare, 1);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 1);
    REQUIRE(output.try_send(std::error_code{}, downloaded_headers{
        .headers = {}, .start_height = 1u, .source_peer = nullptr}));

    // Ceiling at 100; the request is above it.
    ::asio::co_spawn(ioc, header_download_task(input, output, 100u), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{spare}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{200u, null_hash, std::nullopt}));
    drain(ioc);

    // Nobody was asked: the ceiling answers before peer selection.
    CHECK(answers_left(spare) == 2u);

    std::optional<header_download_output> occupant;
    REQUIRE(output.try_receive([&](std::error_code, header_download_output value) {
        occupant = std::move(value);
    }));
    drain(ioc);

    CHECK(header_batches(output) == std::vector<size_t>{0u});
}

TEST_CASE("header tip: a confirmed tip is provisional and the peers become eligible again",
          "[header_tip]") {
    // The requirement that outlives the wedge. The chain moves on whether or
    // not anyone tells this node -- and today nobody does, since block
    // announcements are received and never observed (#706). So a confirmation
    // that ended the walk for good would be a slower version of the same
    // defect: correct at the instant it was made, wrong a block later.
    //
    // A request that names no spent peer is what starts a fresh walk, and it is
    // exactly what header_recheck_task sends.
    ::asio::io_context ioc;
    network::settings config;

    auto only = make_peer(ioc, config, 7301);
    claim_height(only, 965489u);

    // Nothing, then the header that appeared while the node was not looking.
    preload_headers_response(only, 0);
    preload_headers_response(only, 1);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{only}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // The tip was confirmed, and the peer is spent.
    REQUIRE(header_batches(output) == std::vector<size_t>{0u});


    // The producer comes back. Nothing has changed about the peer set, so a
    // task that kept the peer spent has nobody to ask and produces nothing.
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // It was asked again, and this time it had the new header.
    CHECK(header_batches(output) == std::vector<size_t>{1u});

    // Both answers consumed: two requests reached this peer, not one.
    CHECK(answers_left(only) == 0u);
}

TEST_CASE("header tip: a request nobody can serve below the claimed tip parks without confirming",
          "[header_tip]") {
    // The other half of settle_without_peer. Here the peer claims a height well
    // above the request, so exhausting the eligible set is NOT a confirmation:
    // the answer is genuinely missing, and reporting the tip reached would tell
    // the coordinator to start block sync over a chain that is short.
    ::asio::io_context ioc;
    network::settings config;

    auto behind = make_peer(ioc, config, 7401);
    claim_height(behind, 999999u);

    preload_two(behind, 0);

    header_download_input_channel input(ioc, 16);
    header_download_output_channel output(ioc, 16);

    ::asio::co_spawn(ioc, header_download_task(input, output, 0), ::asio::detached);

    REQUIRE(input.try_send(std::error_code{}, peers_updated{{behind}}));
    REQUIRE(input.try_send(std::error_code{}, header_request{965489u, null_hash, std::nullopt}));
    drain(ioc);

    // No confirmation: 965489 is far below the 999999 the peer claims, so the
    // eligible set being empty is not agreement — the headers exist and nobody
    // here is serving them. Only a different peer set can change that, which is
    // what `peers_updated` is for, and re-asking these same peers on a timer
    // would not pay the debt, only re-check it.
    CHECK(header_batches(output).empty());
    CHECK(answers_left(behind) == 1u);
}
