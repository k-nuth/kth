// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/node/wallet/history.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
namespace wallet = kth::node::wallet;

namespace {

hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0x5a};
    return sha256_hash(bytes);
}

chain::script script_of(data_chunk bytes) {
    return chain::script(bytes, false);
}

chain::transaction coinbase_paying(data_chunk const& script, uint64_t value, uint32_t salt) {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index},
        script_of({0x01, uint8_t(salt), uint8_t(salt >> 8)}), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(value, script_of(script), chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

struct paid {
    uint64_t value;
    data_chunk script;
    chain::token_data_opt token{};
};

chain::transaction spending(std::vector<chain::point> const& previous, std::vector<paid> const& outputs, uint32_t salt) {
    chain::input::list inputs;
    for (auto const& point : previous) {
        inputs.emplace_back(chain::output_point{point.hash(), point.index()}, script_of({0x01, uint8_t(salt)}), 0xffffffff);
    }
    chain::output::list made;
    for (auto const& out : outputs) {
        made.emplace_back(out.value, script_of(out.script), out.token);
    }
    return chain::transaction(2, 0, std::move(inputs), std::move(made));
}

// Static data: decoded without assertions, which belong to test cases.
data_chunk decoded(std::string_view hex) {
    auto bytes = decode_base16(hex);
    return bytes ? std::move(*bytes) : data_chunk{};
}

data_chunk const p2pkh_a = decoded("76a914000102030405060708090a0b0c0d0e0f1011121388ac");
data_chunk const p2pkh_b = decoded("76a914aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa88ac");
data_chunk const p2sh_c = decoded("a914cccccccccccccccccccccccccccccccccccccccc87");

} // namespace

// Histories
// ----------------------------------------------------------------------------

TEST_CASE("wallet history finds outputs and spends within and across blocks, and rolls back", "[wallet]") {
    wallet::target_state state(wallet::target::of_script(p2pkh_a), 0);
    REQUIRE(state.next_height() == 0u);
    auto const coinbase = coinbase_paying(p2pkh_a, 5000, 1);
    REQUIRE(state.apply_block(1, digest_of(1), {coinbase}));
    REQUIRE(state.synced() == 1u);

    // Block 2 spends the coinbase output and pays the script again, and a
    // transaction earlier in the block (canonical order) spends that output.
    auto const spend = spending({chain::point(coinbase.hash(), 0)}, {{4000, p2pkh_a}, {900, p2pkh_b}}, 2);
    auto const respend = spending({chain::point(spend.hash(), 0)}, {{3900, p2pkh_b}}, 3);
    auto const unrelated = coinbase_paying(p2pkh_b, 1, 4);
    REQUIRE(state.apply_block(2, digest_of(2), {unrelated, respend, spend}));
    REQUIRE(state.history().size() == 3u);
    REQUIRE(state.unspent().empty());
    REQUIRE(state.history()[1].txid == respend.hash());
    REQUIRE(state.history()[1].position == 1u);
    REQUIRE(state.history()[1].inputs.size() == 1u);
    REQUIRE(state.history()[1].inputs[0].value == 4000u);
    REQUIRE(state.history()[2].outputs[0].value == 4000u);
    REQUIRE(state.history()[2].inputs[0].value == 5000u);
    REQUIRE(state.is_spent(chain::point(coinbase.hash(), 0)));

    // A block that touches nothing changes nothing but the height.
    REQUIRE( ! state.apply_block(3, digest_of(3), {coinbase_paying(p2pkh_b, 1, 5)}));
    REQUIRE(state.synced() == 3u);

    REQUIRE(state.rollback(1));
    REQUIRE(state.synced() == 1u);
    REQUIRE(state.history().size() == 1u);
    REQUIRE(state.unspent().size() == 1u);
    REQUIRE( ! state.is_spent(chain::point(coinbase.hash(), 0)));
    REQUIRE( ! state.rollback(5));
}

TEST_CASE("wallet history follows a token category across scripts, and filters tokens", "[wallet]") {
    auto const category = digest_of(31337);
    auto const nft = chain::make_non_fungible(category, chain::capability_t::mut, data_chunk{0x01});
    wallet::target_state state(wallet::target::of_category(category), 5);
    REQUIRE(state.next_height() == 5u);
    auto const mint = spending({chain::point(digest_of(3), 0)}, {{1000, p2sh_c, nft}, {500, p2sh_c}}, 1);
    REQUIRE(state.apply_block(5, digest_of(5), {coinbase_paying(p2pkh_b, 1, 9), mint}));
    auto const moved = spending({chain::point(mint.hash(), 0)}, {{900, p2pkh_b, nft}}, 2);
    REQUIRE(state.apply_block(6, digest_of(6), {coinbase_paying(p2pkh_b, 1, 10), moved}));
    auto const unspent = state.unspent();
    REQUIRE(unspent.size() == 1u);
    REQUIRE(unspent[0].outpoint == chain::point(moved.hash(), 0));
    REQUIRE(unspent[0].script == p2pkh_b);
    REQUIRE(unspent[0].token == chain::token_data_opt{nft});
    REQUIRE(state.history().size() == 2u);

    REQUIRE(wallet::keeps(wallet::token_filter::include, unspent[0].token));
    REQUIRE(wallet::keeps(wallet::token_filter::only, unspent[0].token));
    REQUIRE( ! wallet::keeps(wallet::token_filter::exclude, unspent[0].token));
    REQUIRE(wallet::keeps(wallet::token_filter::exclude, chain::token_data_opt{}));
    REQUIRE( ! wallet::keeps(wallet::token_filter::only, chain::token_data_opt{}));

    // A rollback below the birthday leaves it as if never synced.
    REQUIRE(state.rollback(3));
    REQUIRE( ! state.synced());
    REQUIRE(state.next_height() == 5u);
}

TEST_CASE("wallet unspent outputs are in chain order, as Fulcrum lists them", "[wallet]") {
    wallet::target_state state(wallet::target::of_script(p2pkh_a), 0);
    // Two payments in one block: listed by their position in it, whatever their hashes.
    auto const first = spending({chain::point(digest_of(41), 0)}, {{10, p2pkh_a}}, 41);
    auto const second = spending({chain::point(digest_of(42), 0)}, {{20, p2pkh_a}, {30, p2pkh_a}}, 42);
    REQUIRE(state.apply_block(3, digest_of(3), {coinbase_paying(p2pkh_b, 1, 3), second, first}));
    auto const listed = state.unspent();
    REQUIRE(listed.size() == 3u);
    REQUIRE(listed[0].outpoint == chain::point(second.hash(), 0));
    REQUIRE(listed[1].outpoint == chain::point(second.hash(), 1));
    REQUIRE(listed[2].outpoint == chain::point(first.hash(), 0));
    REQUIRE(listed[2].position == 2u);
}

TEST_CASE("wallet targets have the script filter keys of what they watch", "[wallet]") {
    auto const by_script = wallet::target::of_script(p2pkh_a);
    auto const by_hash = wallet::target::of_script_hash(sha256_hash(p2pkh_a));
    REQUIRE(by_script.key() == chain::script_key(p2pkh_a));
    REQUIRE(by_hash.key() == by_script.key());
    auto const category = digest_of(8);
    REQUIRE(wallet::target::of_category(category).key() == chain::category_key(category));
    chain::output const out(10, script_of(p2pkh_a), chain::token_data_opt{});
    REQUIRE(by_script.owns(out));
    REQUIRE(by_hash.owns(out));
    REQUIRE( ! wallet::target::of_script(p2pkh_b).owns(out));
    REQUIRE( ! wallet::target::of_category(category).owns(out));
    REQUIRE(by_script != by_hash);
}

// Mempool copy
// ----------------------------------------------------------------------------

TEST_CASE("wallet mempool copy knows fees, chains, and what a block removes", "[wallet]") {
    wallet::mempool_copy pool(1 << 20);
    chain::point const funding(digest_of(1), 0);
    auto const parent = spending({funding}, {{1000, p2pkh_a}}, 1);
    auto const child = spending({chain::point(parent.hash(), 0)}, {{800, p2pkh_b}}, 2);
    auto const known = [&](chain::point const& point) -> std::optional<uint64_t> {
        if (point == funding) return 1500;
        return std::nullopt;
    };
    REQUIRE(pool.insert(parent, known));
    REQUIRE( ! pool.insert(parent, known));
    REQUIRE(pool.insert(child, nullptr));
    REQUIRE( ! pool.insert(coinbase_paying(p2pkh_a, 1, 1), known));
    REQUIRE(pool.find(parent.hash())->fee == 500u);
    REQUIRE(pool.find(child.hash())->fee == 200u);
    REQUIRE(pool.has_unconfirmed_parent(child));
    REQUIRE( ! pool.has_unconfirmed_parent(parent));
    REQUIRE(pool.spender(funding) == parent.hash());
    REQUIRE(pool.size() == 2u);

    // An unknown input leaves the fee unknown.
    auto const stranger = spending({chain::point(digest_of(2), 3)}, {{10, p2pkh_a}}, 3);
    REQUIRE(pool.insert(stranger, known));
    REQUIRE( ! pool.find(stranger.hash())->fee);

    // A block that spends the funding output another way removes the parent
    // and its child, and one that confirms a transaction removes it.
    auto const conflict = spending({funding}, {{1400, p2pkh_b}}, 4);
    auto const removed = pool.remove_for_block({coinbase_paying(p2pkh_b, 1, 2), conflict, stranger});
    REQUIRE(removed.size() == 3u);
    REQUIRE(pool.size() == 0u);
    REQUIRE(pool.bytes() == 0u);
    REQUIRE( ! pool.spender(funding));
}

TEST_CASE("wallet mempool copy drops the oldest transactions when full", "[wallet]") {
    auto const first = spending({chain::point(digest_of(1), 0)}, {{10, p2pkh_a}}, 1);
    auto const size = first.serialized_size(true);
    wallet::mempool_copy pool(size * 2);
    REQUIRE(pool.insert(first, nullptr));
    REQUIRE(pool.insert(spending({chain::point(digest_of(2), 0)}, {{10, p2pkh_a}}, 2), nullptr));
    REQUIRE(pool.insert(spending({chain::point(digest_of(3), 0)}, {{10, p2pkh_a}}, 3), nullptr));
    REQUIRE(pool.size() == 2u);
    REQUIRE( ! pool.contains(first.hash()));
    pool.clear();
    REQUIRE(pool.size() == 0u);
}
