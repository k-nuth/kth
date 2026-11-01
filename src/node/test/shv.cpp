// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <chrono>
#include <future>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/math/mmr.hpp>
#include <kth/domain/message/get_shv.hpp>
#include <kth/domain/message/shv.hpp>
#include <kth/node/detail/shv.hpp>
#include <kth/node/full_node.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

namespace {

constexpr uint32_t wire_version = domain::message::version::level::maximum;

template <typename Message>
data_chunk payload_of(Message const& message) {
    data_chunk bytes(message.serialized_size(wire_version));
    byte_writer writer(bytes);
    REQUIRE(message.to_data(writer, wire_version));
    return bytes;
}

domain::message::shv_request request_at(uint64_t height, uint64_t commitment, uint8_t type = 0) {
    hash_digest encoded{};
    for (size_t i = 0; i < sizeof(height); ++i) {
        encoded[i] = uint8_t(height >> (8 * i));
    }
    return {type, commitment, encoded};
}

} // namespace

TEST_CASE("getshv serves connected headers that a light client verifies", "[shv][p2p]") {
    chain_fixture fixture("shv_peer");
    fixture.settings().mmr_index = true;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const genesis = domain::chain::block::genesis_regtest();
    auto previous = genesis.hash();
    auto const base_time = uint32_t(zulu_time()) - 40 * block_spacing;
    std::vector<domain::chain::block> blocks;
    for (uint32_t height = 1; height <= 5; ++height) {
        blocks.push_back(mine_block(previous, height, base_time + height * block_spacing, 0, {}, 0));
        previous = blocks.back().hash();
    }
    REQUIRE(fixture.organizer().add_headers(headers_of(blocks)).headers_added == blocks.size());
    persist_headers(fixture, blocks, 1);

    // Downloaded headers are not an authenticated connected-chain commitment.
    auto const ahead = payload_of(domain::message::get_shv{{request_at(0, 5, 1)}});
    auto const before = node::detail::process_get_shv(chain, ahead, wire_version);
    REQUIRE(before.action == node::detail::shv_action::reply);
    REQUIRE(before.message.responses.empty());
    connect_bodies(fixture, blocks, 1);

    domain::mmr_accumulator client;
    REQUIRE(client.extend(genesis.hash()));
    for (auto const& block : blocks) {
        REQUIRE(client.extend(block.hash()));
    }

    domain::message::get_shv request;
    for (uint64_t height = 0; height <= 5; ++height) {
        request.requests.push_back(request_at(height, 5));
        request.requests.push_back(request_at(height, 5, domain::message::shv_proof_to_root));
    }
    request.requests.push_back({domain::message::shv_by_hash | domain::message::shv_proof_to_root,
        5, blocks[1].hash()});
    auto const result = node::detail::process_get_shv(chain, payload_of(request), wire_version);
    REQUIRE(result.action == node::detail::shv_action::reply);
    REQUIRE(result.message.responses.size() == request.requests.size());

    // Verify what the client deserializes, rather than the server's proof objects.
    auto const bytes = payload_of(result.message);
    byte_reader reader(bytes);
    auto const response = domain::message::shv::from_data(reader, wire_version);
    REQUIRE(response);
    for (size_t i = 0; i < response->responses.size(); ++i) {
        auto const& proof = response->responses[i];
        REQUIRE(proof.type == request.requests[i].type);
        REQUIRE(proof.commitment_height == 5);
        auto const expected_height = i == 12 ? 2 : i / 2;
        REQUIRE(proof.block_height == expected_height);
        auto const expected = expected_height == 0 ? genesis.hash() : blocks[expected_height - 1].hash();
        REQUIRE(domain::chain::hash(proof.header) == expected);
        if ((proof.type & domain::message::shv_proof_to_root) != 0) {
            REQUIRE(proof.target == client.root());
            REQUIRE(client.verify_proof_to_root(proof.block_height, expected, proof.proof));
        } else {
            REQUIRE(client.verify_proof_to_peak(proof.block_height, expected, proof.proof));
            auto peak = expected;
            auto index = proof.block_height;
            for (auto const& sibling : proof.proof) {
                peak = (index & 1) != 0 ? domain::mmr_node_hash(sibling, peak)
                    : domain::mmr_node_hash(peak, sibling);
                index >>= 1;
            }
            REQUIRE(proof.target == peak);
        }
    }

    auto invalid_type = request_at(0, 5, 4);
    auto invalid_height = request_at(0, 5);
    invalid_height.block_height_or_hash[8] = 1;
    domain::message::get_shv mixed{{request_at(0, 5), invalid_type, invalid_height,
        request_at(0, 6), request_at(6, 5), request_at(5, 4),
        {domain::message::shv_by_hash, 5, null_hash}, request_at(5, 5, 1)}};
    auto const filtered = node::detail::process_get_shv(chain, payload_of(mixed), wire_version);
    REQUIRE(filtered.action == node::detail::shv_action::reply);
    REQUIRE(filtered.message.responses.size() == 2);
    CHECK(filtered.message.responses.front().block_height == 0);
    CHECK(filtered.message.responses.back().block_height == 5);
}

TEST_CASE("getshv enforces batch and payload boundaries", "[shv][p2p]") {
    chain_fixture fixture("shv_limits");
    fixture.settings().mmr_index = true;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& chain = fixture.chain();
    using node::detail::process_get_shv;
    using node::detail::shv_action;

    for (auto const& bytes : std::vector<data_chunk>{{101}, {0xfe, 0xff, 0xff, 0xff, 0x7f}}) {
        auto const result = process_get_shv(chain, bytes, wire_version);
        REQUIRE(result.action == shv_action::oversized);
        REQUIRE(result.message.responses.empty());
    }
    domain::message::get_shv maximum;
    maximum.requests.assign(domain::message::max_shv_requests, request_at(0, 0, 1));
    auto const accepted = process_get_shv(chain, payload_of(maximum), wire_version);
    REQUIRE(accepted.action == shv_action::reply);
    REQUIRE(accepted.message.responses.size() == domain::message::max_shv_requests);
    auto const empty = process_get_shv(chain, data_chunk{0}, wire_version);
    REQUIRE(empty.action == shv_action::reply);
    REQUIRE(empty.message.responses.empty());

    auto const bytes = payload_of(domain::message::get_shv{{request_at(0, 0, 1)}});
    for (size_t split = 0; split < bytes.size(); ++split) {
        INFO("request prefix " << split);
        REQUIRE(process_get_shv(chain, byte_span(bytes).first(split), wire_version).action == shv_action::malformed);
    }
    for (auto const& malformed : std::vector<data_chunk>{{0xfd}, {0xfd, 1, 0}, {0, 0}}) {
        REQUIRE(process_get_shv(chain, malformed, wire_version).action == shv_action::malformed);
    }
}

TEST_CASE("getshv is ignored when its index is disabled", "[shv][p2p]") {
    chain_fixture fixture("shv_disabled");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    for (auto const& bytes : std::vector<data_chunk>{{}, {101}, {0}}) {
        auto const result = node::detail::process_get_shv(fixture.chain(), bytes, wire_version);
        REQUIRE(result.action == node::detail::shv_action::ignored);
        REQUIRE(result.message.responses.empty());
    }
}

TEST_CASE("NODE_SHV follows the index setting in the version handshake", "[shv][p2p]") {
    auto const bit = uint64_t(domain::message::version::service::node_shv);
    REQUIRE(bit == (uint64_t{1} << 9));
    for (bool enabled : {false, true}) {
        node::configuration configuration(domain::config::network::regtest);
        configuration.chain.mmr_index = enabled;
        configuration.network.threads = 1;
        configuration.chain.cores = 1;
        configuration.network.services = domain::message::version::service::node_network | bit;
        configuration.network.peers_file.clear();
        node::full_node node(configuration);
        auto const handshake = network::make_handshake_config(node.network_settings(), 0, 1);
        CHECK(((handshake.services & bit) != 0) == enabled);
        CHECK((handshake.services & domain::message::version::service::node_network) != 0);
    }
}

TEST_CASE("getshv handler penalizes oversized batches without sending a reply", "[shv][p2p]") {
    for (bool enabled : {false, true}) {
        chain_fixture fixture(enabled ? "shv_score_on" : "shv_score_off");
        fixture.settings().mmr_index = enabled;
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        network::settings settings;
        settings.threads = 1;
        settings.peers_file.clear();
        node::p2p_node p2p(settings);
        ::asio::io_context io;
        // No socket is opened. The handler's oversized path must neither send
        // nor wait for a peer to read, even when its score reaches the ban limit.
        network::peer_session peer(::asio::ip::tcp::socket(io), settings);
        auto const handler = node::detail::make_get_shv_handler(fixture.chain(), p2p);
        network::raw_message const raw{domain::message::heading(0, "getshv", 1, 0), {101}};
        for (int attempt = 1; attempt <= 5; ++attempt) {
            io.restart();
            auto result = ::asio::co_spawn(io, handler(peer, raw), ::asio::use_future);
            io.run_for(std::chrono::seconds(2));
            REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
            REQUIRE(result.get() == (enabled && attempt == 5
                ? node::message_result::disconnect : node::message_result::handled));
            auto const record = p2p.peer_db().get(peer.authority());
            if (enabled) {
                REQUIRE(record);
                REQUIRE(record->reputation_score == attempt * 20);
            } else {
                REQUIRE( ! record);
            }
        }
    }
}
