// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>

#include <kth/domain/math/mmr.hpp>
#include <kth/domain/message/get_shv.hpp>
#include <kth/domain/message/shv.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/node/detail/shv.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

namespace {

constexpr auto wire_version = domain::message::version::level::maximum;

domain::message::shv_request request_at(uint64_t height, uint64_t commitment, uint8_t type) {
    hash_digest encoded{};
    for (size_t i = 0; i < sizeof(height); ++i) {
        encoded[i] = uint8_t(height >> (i * 8));
    }
    return {type, commitment, encoded};
}

::asio::awaitable<void> write_fragmented(::asio::ip::tcp::socket& client,
    data_chunk const& frame, ::asio::io_context& io) {
    // Yield between writes so the receiver must retain an incomplete heading
    // and an incomplete payload before it can dispatch either message.
    auto const first_payload = domain::message::heading::satoshi_fixed_size() + 3;
    auto const split = std::min(first_payload, frame.size());
    for (size_t i = 0; i < split; ++i) {
        auto const [ec, written] = co_await ::asio::async_write(client,
            ::asio::buffer(frame.data() + i, 1), ::asio::as_tuple(::asio::use_awaitable));
        REQUIRE( ! ec);
        REQUIRE(written == 1);
        ::asio::steady_timer pause(io, std::chrono::milliseconds(1));
        auto const [wait_ec] = co_await pause.async_wait(::asio::as_tuple(::asio::use_awaitable));
        REQUIRE( ! wait_ec);
    }
    if (split < frame.size()) {
        auto const [ec, written] = co_await ::asio::async_write(client,
            ::asio::buffer(frame.data() + split, frame.size() - split),
            ::asio::as_tuple(::asio::use_awaitable));
        REQUIRE( ! ec);
        REQUIRE(written == frame.size() - split);
    }
    co_return;
}

::asio::awaitable<network::raw_message> read_frame(::asio::ip::tcp::socket& client,
    uint32_t magic) {
    std::array<uint8_t, 24> bytes{};
    auto const [head_ec, head_size] = co_await ::asio::async_read(client, ::asio::buffer(bytes),
        ::asio::as_tuple(::asio::use_awaitable));
    REQUIRE( ! head_ec);
    REQUIRE(head_size == bytes.size());
    byte_reader reader(bytes);
    auto heading = domain::message::heading::from_data(reader, wire_version);
    REQUIRE(heading);
    REQUIRE(heading->magic() == magic);
    REQUIRE(heading->payload_size() <= 4096);
    data_chunk payload(heading->payload_size());
    auto const [payload_ec, payload_size] = co_await ::asio::async_read(client,
        ::asio::buffer(payload), ::asio::as_tuple(::asio::use_awaitable));
    REQUIRE( ! payload_ec);
    REQUIRE(payload_size == payload.size());
    REQUIRE(heading->checksum() == bitcoin_checksum(payload));
    co_return network::raw_message{std::move(*heading), std::move(payload)};
}

struct transport_stop {
    ::asio::ip::tcp::acceptor& listener;
    ::asio::ip::tcp::socket& client;
    ::asio::steady_timer& deadline;
    network::peer_session::ptr& peer;

    void stop() const {
        deadline.cancel();
        boost_code ignored;
        listener.close(ignored);
        client.close(ignored);
        if (peer) {
            peer->stop();
        }
    }

    ~transport_stop() { stop(); }
};

} // namespace

TEST_CASE("SHV framed peer requests survive split reads and refuse invalid entries", "[shv][p2p][transport]") {
    for (bool enabled : {false, true}) {
        chain_fixture fixture(enabled ? "shv_transport_on" : "shv_transport_off");
        fixture.settings().mmr_index = enabled;
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());

        auto const genesis = domain::chain::block::genesis_regtest();
        auto const base_time = uint32_t(zulu_time()) - 10 * block_spacing;
        std::vector<domain::chain::block> blocks;
        auto previous = genesis.hash();
        for (uint32_t height = 1; height <= 2; ++height) {
            blocks.push_back(mine_block(previous, height, base_time + height * block_spacing, 0, {}, 0));
            previous = blocks.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == blocks.size());
        persist_headers(fixture, blocks, 1);
        connect_bodies(fixture, blocks, 1);

        network::settings settings(domain::config::network::regtest);
        settings.threads = 1;
        settings.peers_file.clear();
        settings.validate_checksum = true;
        node::p2p_node p2p(settings);
        node::message_dispatcher dispatcher;
        dispatcher.register_handler(domain::message::get_shv::command,
            node::detail::make_get_shv_handler(fixture.chain(), p2p));
        // Spelled as the handler type: a lambda with no captures also converts
        // to the function-pointer overload.
        dispatcher.register_handler<domain::message::ping>(
            node::typed_message_handler_fn<domain::message::ping>{
                [](network::peer_session& peer, domain::message::ping const& ping)
                    -> ::asio::awaitable<node::message_result> {
                    auto const ec = co_await peer.send(domain::message::pong(ping.nonce()));
                    co_return ec ? node::message_result::disconnect : node::message_result::handled;
                }});

        ::asio::io_context io;
        ::asio::ip::tcp::acceptor listener(io,
            {::asio::ip::address_v4::loopback(), 0});
        // The listener itself reports readiness and its OS-assigned port.
        auto const endpoint = listener.local_endpoint();
        REQUIRE(endpoint.port() != 0);
        ::asio::ip::tcp::socket client(io);
        ::asio::steady_timer deadline(io, std::chrono::seconds(10));
        network::peer_session::ptr peer;
        std::future<code> peer_run;
        bool timed_out = false;
        transport_stop const cleanup{listener, client, deadline, peer};
        deadline.async_wait([&](auto const& ec) {
            if ( ! ec) {
                timed_out = true;
                cleanup.stop();
            }
        });

        auto serve = [&]() -> ::asio::awaitable<void> {
            auto [accept_ec, socket] = co_await listener.async_accept(
                ::asio::as_tuple(::asio::use_awaitable));
            REQUIRE( ! accept_ec);
            peer = std::make_shared<network::peer_session>(std::move(socket), settings, true);
            peer_run = ::asio::co_spawn(io, peer->run(), ::asio::use_future);
            for (;;) {
                auto [ec, message] = co_await peer->messages().async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    break;
                }
                if ( ! co_await dispatcher.dispatch(*peer, message)) {
                    break;
                }
            }
            peer->stop();
            co_return;
        };
        auto server = ::asio::co_spawn(io, serve(), ::asio::use_future);

        auto exchange = [&]() -> ::asio::awaitable<void> {
            // Teardown also runs when an assertion or I/O failure unwinds this
            // coroutine, so a missing reply cannot strand the session's waits.
            transport_stop const finished{listener, client, deadline, peer};
            auto const [connect_ec] = co_await client.async_connect(endpoint,
                ::asio::as_tuple(::asio::use_awaitable));
            REQUIRE( ! connect_ec);
            auto reserved = request_at(1, 2, 4);
            auto upper = request_at(1, 2, 0);
            upper.block_height_or_hash[8] = 1;
            domain::message::get_shv const request{{request_at(1, 2, 1), reserved, upper,
                request_at(1, 2, 0)}};
            auto const request_frame = domain::message::serialize(wire_version, request, settings.identifier);
            co_await write_fragmented(client, request_frame, io);
            if (enabled) {
                auto const raw = co_await read_frame(client, settings.identifier);
                REQUIRE(raw.heading.command() == "shv");
                auto const response = from_data_chunk<domain::message::shv>(raw.payload, wire_version);
                REQUIRE(response);
                REQUIRE(response->responses.size() == 2);
                domain::mmr_accumulator accumulator;
                REQUIRE(accumulator.extend(genesis.hash()));
                for (auto const& block : blocks) {
                    REQUIRE(accumulator.extend(block.hash()));
                }
                auto const& root = response->responses[0];
                REQUIRE(root.type == 1);
                REQUIRE(root.block_height == 1);
                REQUIRE(root.commitment_height == 2);
                REQUIRE(domain::chain::hash(root.header) == blocks[0].hash());
                REQUIRE(root.target == accumulator.root());
                REQUIRE(accumulator.verify_proof_to_root(1, blocks[0].hash(), root.proof));
                auto const& peak = response->responses[1];
                REQUIRE(peak.type == 0);
                REQUIRE(peak.block_height == 1);
                REQUIRE(accumulator.verify_proof_to_peak(1, blocks[0].hash(), peak.proof));
                REQUIRE(peak.target == domain::mmr_node_hash(genesis.hash(), blocks[0].hash()));

                domain::message::get_shv const invalid{{reserved, upper,
                    request_at(1, 3, 0), request_at(3, 2, 0),
                    {domain::message::shv_by_hash, 2, null_hash}}};
                auto const frame = domain::message::serialize(wire_version, invalid, settings.identifier);
                co_await write_fragmented(client, frame, io);
                auto const omitted = co_await read_frame(client, settings.identifier);
                REQUIRE(omitted.heading.command() == "shv");
                auto const parsed = from_data_chunk<domain::message::shv>(omitted.payload, wire_version);
                REQUIRE(parsed);
                REQUIRE(parsed->responses.empty());
            }

            // Split a noncanonical CompactSize after its width marker. It
            // must not produce a proof or shift parsing into the next frame.
            data_chunk noncanonical{1, 1, 0xfd, 2, 0};
            noncanonical.resize(noncanonical.size() + hash_size);
            domain::message::heading const malformed_heading(settings.identifier, "getshv",
                uint32_t(noncanonical.size()), bitcoin_checksum(noncanonical));
            auto malformed_frame = malformed_heading.to_data();
            malformed_frame.insert(malformed_frame.end(), noncanonical.begin(), noncanonical.end());
            co_await write_fragmented(client, malformed_frame, io);

            domain::message::get_shv oversized;
            oversized.requests.resize(domain::message::max_shv_requests + 1);
            auto const oversized_frame = domain::message::serialize(wire_version, oversized, settings.identifier);
            co_await write_fragmented(client, oversized_frame, io);
            auto const fence = domain::message::serialize(wire_version,
                domain::message::ping(0x12345678), settings.identifier);
            co_await write_fragmented(client, fence, io);
            // Dispatch is ordered. A forbidden reply to either ignored request
            // would arrive before this pong and fail the command assertion.
            auto const pong = co_await read_frame(client, settings.identifier);
            REQUIRE(pong.heading.command() == "pong");
            auto const echoed = from_data_chunk<domain::message::pong>(pong.payload, wire_version);
            REQUIRE(echoed);
            REQUIRE(echoed->nonce() == 0x12345678);
            REQUIRE(peer);
            auto const record = p2p.peer_db().get(peer->authority());
            if (enabled) {
                REQUIRE(record);
                REQUIRE(record->reputation_score == 20);
            } else {
                REQUIRE( ! record);
            }
            co_return;
        };
        auto client_result = ::asio::co_spawn(io, exchange(), ::asio::use_future);
        io.run_for(std::chrono::seconds(12));
        cleanup.stop();
        io.restart();
        io.run_for(std::chrono::seconds(1));

        REQUIRE(client_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        REQUIRE(server.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        REQUIRE(peer_run.valid());
        REQUIRE(peer_run.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        (void)peer_run.get();
        server.get();
        client_result.get();
        REQUIRE_FALSE(timed_out);
    }
}
