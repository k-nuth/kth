// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"
#include "chain_data_fixtures.hpp"
#include "header_peer_harness.hpp"
#include "tx_relay_harness.hpp"

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <asio/io_context.hpp>

#include <kth/domain/chain/partial_merkle_tree.hpp>
#include <kth/domain/message/filter_add.hpp>
#include <kth/domain/message/filter_clear.hpp>
#include <kth/domain/message/filter_load.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/network/tx_inventory.hpp>
#include <kth/node/detail/bloom_filters.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/handlers/chain_data.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/parser.hpp>

using namespace kth;
using test::loopback_peer;
using test::make_peer;
using test::relay_settings;
using test::serve;
using test::served_chain;
using test::unknown_hash;
using domain::chain::bloom_filter;
using node::detail::apply_filter_message;
using node::detail::filter_change;

namespace {

constexpr auto wire_version = domain::message::version::level::maximum;

// A `filterload` payload: the bit field with its length, the number of hash
// functions, the tweak and the flags.
data_chunk load_payload(size_t bytes, uint32_t functions, uint32_t tweak = 0, uint8_t flags = 1) {
    data_chunk out;
    if (bytes < 0xfd) {
        out.push_back(uint8_t(bytes));
    } else {
        out.push_back(0xfd);
        out.push_back(uint8_t(bytes & 0xff));
        out.push_back(uint8_t(bytes >> 8));
    }
    out.insert(out.end(), bytes, 0x00);
    for (auto const value : {functions, tweak}) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(uint8_t(value >> shift));
    }
    out.push_back(flags);
    return out;
}

data_chunk add_payload(size_t bytes) {
    data_chunk out;
    if (bytes < 0xfd) {
        out.push_back(uint8_t(bytes));
    } else {
        out.push_back(0xfd);
        out.push_back(uint8_t(bytes & 0xff));
        out.push_back(uint8_t(bytes >> 8));
    }
    out.insert(out.end(), bytes, 0x42);
    return out;
}

filter_change apply(network::tx_inventory& inventory, std::string const& command, data_chunk const& payload) {
    return apply_filter_message(inventory, command, payload, wire_version);
}

network::raw_message wire(std::string_view command, data_chunk payload) {
    network::raw_message raw;
    raw.heading = domain::message::heading(0xdab5bffa, command, uint32_t(payload.size()), 0);
    raw.payload = std::move(payload);
    return raw;
}

// A node that offers bloom filters: the service bit is what it goes by.
network::settings offering_settings() {
    auto settings = relay_settings();
    settings.services |= domain::message::version::service::node_bloom;
    return settings;
}

} // namespace

TEST_CASE("filterload sets the peer's filter and asks for announcements", "[bloom filters]") {
    network::tx_inventory inventory;
    // From the start: the filter of no bytes, which holds everything.
    CHECK(inventory.filter() == bloom_filter{});
    CHECK(inventory.filter().contains(data_chunk{0x01}));
    CHECK_FALSE(inventory.relay_requested());

    REQUIRE(apply(inventory, "filterload", load_payload(16, 5, 7, 2)) == filter_change::loaded);
    CHECK(inventory.filter().bits() == data_chunk(16, 0));
    CHECK(inventory.filter().hash_functions() == 5);
    CHECK(inventory.filter().tweak() == 7);
    CHECK(inventory.filter().flags() == 2);
    CHECK_FALSE(inventory.filter().contains(data_chunk{0x01}));
    CHECK(inventory.relay_requested());

    // A second one replaces the first.
    REQUIRE(apply(inventory, "filterload", load_payload(4, 1)) == filter_change::loaded);
    CHECK(inventory.filter().bits().size() == 4);

    // The largest a peer may load.
    CHECK(apply(inventory, "filterload", load_payload(bloom_filter::max_size, 50)) == filter_change::loaded);
    CHECK(inventory.filter().bits().size() == bloom_filter::max_size);
}

TEST_CASE("filterload beyond a limit is refused and leaves the filter as it was", "[bloom filters]") {
    network::tx_inventory inventory;
    REQUIRE(apply(inventory, "filterload", load_payload(4, 1)) == filter_change::loaded);

    CHECK(apply(inventory, "filterload", load_payload(bloom_filter::max_size + 1, 1)) == filter_change::refused);
    CHECK(apply(inventory, "filterload", load_payload(4, 51)) == filter_change::refused);
    CHECK(inventory.filter().bits().size() == 4);
    CHECK(inventory.filter().hash_functions() == 1);

    // One that does not parse changes nothing and is not scored.
    auto cut = load_payload(4, 1);
    cut.pop_back();
    CHECK(apply(inventory, "filterload", cut) == filter_change::none);
    CHECK(apply(inventory, "filterload", {}) == filter_change::none);
    CHECK(inventory.filter().bits().size() == 4);
}

TEST_CASE("filteradd extends a loaded filter", "[bloom filters]") {
    network::tx_inventory inventory;
    // Before any filter is loaded the peer's filter holds everything already:
    // an element changes nothing and is no offence (BCHN).
    CHECK(apply(inventory, "filteradd", add_payload(20)) == filter_change::added);
    CHECK(inventory.filter() == bloom_filter{});

    REQUIRE(apply(inventory, "filterload", load_payload(32, 4)) == filter_change::loaded);
    data_chunk const element(20, 0x42);
    CHECK_FALSE(inventory.filter().contains(element));
    REQUIRE(apply(inventory, "filteradd", add_payload(20)) == filter_change::added);
    CHECK(inventory.filter().contains(element));

    // The largest element, and one byte more.
    CHECK(apply(inventory, "filteradd", add_payload(bloom_filter::max_element_size)) == filter_change::added);
    CHECK(apply(inventory, "filteradd", add_payload(bloom_filter::max_element_size + 1)) == filter_change::refused);
    // Cut short: nothing, and not scored.
    auto cut = add_payload(20);
    cut.pop_back();
    CHECK(apply(inventory, "filteradd", cut) == filter_change::none);
}

TEST_CASE("filterclear puts back the filter that holds everything and keeps announcements on", "[bloom filters]") {
    network::tx_inventory inventory;
    // With nothing loaded it still asks for announcements.
    CHECK(apply(inventory, "filterclear", {}) == filter_change::cleared);
    CHECK(inventory.filter() == bloom_filter{});
    CHECK(inventory.relay_requested());

    REQUIRE(apply(inventory, "filterload", load_payload(8, 2)) == filter_change::loaded);
    CHECK_FALSE(inventory.filter().contains(data_chunk{0x01}));
    CHECK(apply(inventory, "filterclear", {}) == filter_change::cleared);
    CHECK(inventory.filter() == bloom_filter{});
    CHECK(inventory.filter().contains(data_chunk{0x01}));

    // Any other command is not a filter message.
    CHECK(apply(inventory, "feefilter", {}) == filter_change::none);
    CHECK(apply(inventory, "filter", {}) == filter_change::none);
}

TEST_CASE("a peer wants every transaction until it loads a filter, and then those it matches", "[bloom filters]") {
    auto const base_time = uint32_t(zulu_time()) - 20u * test::block_spacing;
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        base_time, 0, {}, 0);
    auto const& coinbase = block.transactions().front();

    network::tx_inventory inventory;
    CHECK(inventory.wants(coinbase));

    auto empty = *bloom_filter::sized(10, 0.000001, 0, bloom_filter::update_all);
    inventory.load_filter(empty);
    CHECK_FALSE(inventory.wants(coinbase));

    auto watching = *bloom_filter::sized(10, 0.000001, 0, bloom_filter::update_all);
    watching.insert(coinbase.hash());
    inventory.load_filter(watching);
    CHECK(inventory.wants(coinbase));

    inventory.clear_filter();
    CHECK(inventory.wants(coinbase));
}

TEST_CASE("a filtered block proves its matched transactions and names them", "[bloom filters]") {
    auto const base_time = uint32_t(zulu_time()) - 20u * test::block_spacing;
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        base_time, 0, {}, 0);
    REQUIRE(block.transactions().size() == 1);
    auto const txid = block.transactions().front().hash();

    // The filter every peer starts with: every transaction is a match.
    bloom_filter everything;
    auto const whole = node::detail::filter_block(block, everything);
    CHECK(whole.matched == std::vector<size_t>{0});
    CHECK(whole.proof.hashes() == hash_list{txid});
    CHECK(whole.proof.flags() == data_chunk{0x01});
    CHECK(everything == bloom_filter{});

    // Nothing matches: the merkle root alone, and no transaction.
    auto nothing = *bloom_filter::sized(10, 0.000001, 0, bloom_filter::update_all);
    auto const unmatched = node::detail::filter_block(block, nothing);
    CHECK(unmatched.matched.empty());
    CHECK(unmatched.proof.header() == block.header());
    CHECK(unmatched.proof.total_transactions() == 1);
    CHECK(unmatched.proof.hashes() == hash_list{block.header().merkle()});
    CHECK(unmatched.proof.flags() == data_chunk{0x00});

    // The coinbase matches by its hash.
    auto watching = *bloom_filter::sized(10, 0.000001, 0, bloom_filter::update_all);
    watching.insert(txid);
    auto const matched = node::detail::filter_block(block, watching);
    CHECK(matched.matched == std::vector<size_t>{0});
    CHECK(matched.proof.hashes() == hash_list{txid});
    CHECK(matched.proof.flags() == data_chunk{0x01});

    // What a wallet does with it: the proof leads to the header's root.
    domain::chain::partial_merkle_tree const tree{uint32_t(matched.proof.total_transactions()),
        matched.proof.hashes(), matched.proof.flags()};
    auto const proved = tree.extract();
    REQUIRE(proved);
    CHECK(proved->root == block.header().merkle());
    REQUIRE(proved->matches.size() == 1);
    CHECK(proved->matches.front().second == txid);
}

TEST_CASE("getdata for a filtered block sends the proof and the matched transactions", "[bloom filters][chain_data]") {
    served_chain c;
    using domain::chain::bloom_filter;
    using domain::message::inventory_vector;
    using type_id = inventory_vector::type_id;
    auto const coinbase = c.trunk[1].transactions().front().hash();
    auto const request = domain::message::get_data::create({
        {type_id::filtered_block, c.trunk[1].hash()},     // its coinbase is watched
        {type_id::filtered_block, c.trunk[2].hash()},     // nothing in it is
        {type_id::filtered_block, c.side.hash()},         // off the connected chain
        {type_id::filtered_block, unknown_hash()}});
    REQUIRE(request);

    SECTION("with no filter loaded: every transaction of each block held, as BCHN answers") {
        loopback_peer peer;
        CHECK(serve(peer, node::handlers::chain_data::handle_get_data(c.fixture.chain(), *peer.session, *request,
            true)) ==
            node::message_result::handled);
        auto const sent = peer.received();
        // A proof and the one transaction for each of the two blocks, then
        // what is not served.
        REQUIRE(sent.size() == 5);
        CHECK(sent[0].command == domain::message::merkle_block::command);
        CHECK(sent[1].command == domain::message::transaction::command);
        CHECK(sent[2].command == domain::message::merkle_block::command);
        CHECK(sent[3].command == domain::message::transaction::command);
        CHECK(sent[4].command == domain::message::not_found::command);
        byte_reader reader(sent[0].payload);
        auto const first = domain::message::merkle_block::from_data(reader, peer.session->negotiated_version());
        REQUIRE(first);
        CHECK(first->hashes() == hash_list{coinbase});
        CHECK(first->flags() == data_chunk{0x01});
    }
    SECTION("with a filter: a merkleblock for each block held, and what it matched") {
        loopback_peer peer;
        auto filter = *bloom_filter::sized(10, 0.000001, 0, bloom_filter::update_all);
        filter.insert(coinbase);
        peer.session->tx_inventory().load_filter(filter);
        CHECK(serve(peer, node::handlers::chain_data::handle_get_data(c.fixture.chain(), *peer.session, *request,
            true)) ==
            node::message_result::handled);

        auto const sent = peer.received();
        REQUIRE(sent.size() == 4);
        auto const version = peer.session->negotiated_version();

        REQUIRE(sent[0].command == domain::message::merkle_block::command);
        byte_reader first_reader(sent[0].payload);
        auto const first = domain::message::merkle_block::from_data(first_reader, version);
        REQUIRE(first);
        CHECK(domain::chain::hash(first->header()) == c.trunk[1].hash());
        CHECK(first->total_transactions() == 1);
        CHECK(first->hashes() == hash_list{coinbase});
        CHECK(first->flags() == data_chunk{0x01});

        REQUIRE(sent[1].command == domain::message::transaction::command);
        byte_reader tx_reader(sent[1].payload);
        auto const tx = domain::message::transaction::from_data(tx_reader, version);
        REQUIRE(tx);
        CHECK(tx->hash() == coinbase);
        // It was sent, so it is not announced to the peer again.
        CHECK(peer.session->tx_inventory().known(coinbase));

        REQUIRE(sent[2].command == domain::message::merkle_block::command);
        byte_reader second_reader(sent[2].payload);
        auto const second = domain::message::merkle_block::from_data(second_reader, version);
        REQUIRE(second);
        CHECK(domain::chain::hash(second->header()) == c.trunk[2].hash());
        CHECK(second->flags() == data_chunk{0x00});

        REQUIRE(sent[3].command == domain::message::not_found::command);
        byte_reader missing_reader(sent[3].payload);
        auto const missing = domain::message::not_found::from_data(missing_reader, version);
        REQUIRE(missing);
        CHECK(missing->inventories() == inventory_vector::list{
            {type_id::filtered_block, c.side.hash()},
            {type_id::filtered_block, unknown_hash()}});
    }
}

TEST_CASE("a node that offers bloom filters takes the filter messages of a peer", "[bloom filters][routing]") {
    ::asio::io_context ioc;
    auto const settings = offering_settings();
    node::p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9601);

    CHECK(node.observe_transaction_message(peer,
        wire(domain::message::filter_load::command, load_payload(64, 5))));
    CHECK_FALSE(peer->stopped());
    CHECK(peer->tx_inventory().filter().bits().size() == 64);
    CHECK(peer->tx_inventory().relay_requested());

    CHECK(node.observe_transaction_message(peer,
        wire(domain::message::filter_add::command, add_payload(20))));
    CHECK_FALSE(peer->stopped());

    CHECK(node.observe_transaction_message(peer,
        wire(domain::message::filter_clear::command, data_chunk{})));
    CHECK_FALSE(peer->stopped());
    CHECK(peer->tx_inventory().filter().bits().empty());
}

TEST_CASE("a filter message that breaks a limit ends the session of the peer", "[bloom filters][routing]") {
    ::asio::io_context ioc;
    auto const settings = offering_settings();
    node::p2p_node node(settings);
    auto peer = make_peer(ioc, settings, 9602);

    SECTION("a filter over 36,000 bytes") {
        CHECK(node.observe_transaction_message(peer,
            wire(domain::message::filter_load::command, load_payload(36001, 5))));
    }
    SECTION("an element over 520 bytes") {
        CHECK(node.observe_transaction_message(peer,
            wire(domain::message::filter_add::command, add_payload(521))));
    }
    CHECK(peer->stopped());
    CHECK(peer->tx_inventory().filter().bits().empty());
}

TEST_CASE("bloom filters are on unless turned off in a build with the feature", "[bloom filters][parser]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK(defaults.configured.node.bloom_filters);

    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--node.bloom_filters", "false"};
    std::ostringstream error;
    REQUIRE(parser.parse(5, argv, error));
    CHECK_FALSE(parser.configured.node.bloom_filters);
}

TEST_CASE("a node announces bloom filters when it serves them", "[bloom filters][node]") {
    auto const bloom = uint64_t(domain::message::version::service::node_bloom);
    auto const dir = std::filesystem::temp_directory_path() / "kth_bloom_node_served";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    auto const config = [&dir](bool filters) {
        node::configuration cfg{domain::config::network::regtest};
        cfg.database.directory = dir;
        cfg.database.db_max_size = 16ULL << 20;
        cfg.network.threads = 0;
        cfg.network.peers_file = dir / "peers.dat";
        cfg.network.hosts_file = dir / "hosts.dat";
        cfg.node.bloom_filters = filters;
        return cfg;
    };

    {
        node::full_node node{config(true)};
        CHECK((node.network_settings().services & bloom) != 0);
    }
    {
        node::full_node node{config(false)};
        CHECK((node.network_settings().services & bloom) == 0);
    }
    std::filesystem::remove_all(dir, ec);
}
