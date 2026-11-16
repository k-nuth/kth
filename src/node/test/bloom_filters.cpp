// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include "sync_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <kth/domain/chain/partial_merkle_tree.hpp>
#include <kth/domain/message/filter_add.hpp>
#include <kth/domain/message/filter_clear.hpp>
#include <kth/domain/message/filter_load.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/network/tx_inventory.hpp>
#include <kth/node/detail/bloom_filters.hpp>

using namespace kth;
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
