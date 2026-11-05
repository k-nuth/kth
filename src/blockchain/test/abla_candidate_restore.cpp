// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure/utility/timer.hpp>
#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::test;

#if defined(KTH_CURRENCY_BCH)
namespace {
namespace abla = domain::chain::abla;

void seed_connected_header(chain_fixture& fixture, uint64_t size, uint64_t epsilon, uint64_t beta) {
    auto const block = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 600, 1, {}, 0);
    database::settings settings;
    settings.directory = fixture.dir();
    database::data_base db(settings);
    REQUIRE(db.open());
    REQUIRE(!db.push_header(block.header(), 1, size, epsilon, beta));
    REQUIRE(db.internal_db().publish_transition(database::transition_heights{
        .last_block_height = 1, .utxo_built_height = 1}) == database::result_code::success);
    REQUIRE(db.close());
    // The fixture seeds an already-connected trusted record, rather than
    // running block connection. Exclude that checkpoint from undo coverage;
    // the observable under test is the restored candidate, not missing undo.
    fixture.settings().max_checkpoint_height = 1;
}
} // namespace

TEST_CASE("restoring a persisted ABLA tip advances its candidate exactly once", "[abla][candidate_restore]") {
    chain_fixture fixture("abla_candidate_restore");
    REQUIRE(fixture.created());
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    fixture.settings().cantor_activation_time = UINT64_MAX;
#endif
    auto const& config = fixture.settings().abla_config;
    abla::state seed(config, 64'000'000);
    seed.control_block_size = 32'000'000;
    seed.elastic_buffer_size = 32'000'000;
    auto const candidate = abla::next(seed, config, 0);
    REQUIRE(candidate.has_value());
    auto const expected = abla::block_size_limit(*candidate);
    REQUIRE(expected > static_max_block_size(domain::config::network::regtest));
    REQUIRE(expected != abla::block_size_limit(seed));
    seed_connected_header(fixture, seed.block_size, seed.control_block_size, seed.elastic_buffer_size);

    REQUIRE(fixture.start());
    auto const state = fixture.chain().chain_state();
    REQUIRE(state);
    CHECK(state->height() == 2);
    CHECK(state->dynamic_max_block_size() == expected);
    REQUIRE(fixture.restart());
    auto const restored = fixture.chain().chain_state();
    REQUIRE(restored);
    CHECK(restored->height() == 2);
    CHECK(restored->dynamic_max_block_size() == expected);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("a faster block candidate refuses an unknown persisted parent state", "[abla][candidate_restore][faster_blocks]") {
    chain_fixture fixture("abla_unknown_faster");
    REQUIRE(fixture.created());
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    fixture.settings().cantor_activation_time = 0;
#endif
    seed_connected_header(fixture, 0, 0, 0);
    CHECK_FALSE(fixture.start());
}

TEST_CASE("a legacy unknown ABLA tip cannot become a trusted faster block parent", "[abla][candidate_restore][migration]") {
    chain_fixture fixture("abla_unknown_legacy");
    REQUIRE(fixture.created());
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    fixture.settings().cantor_activation_time = UINT64_MAX;
#endif
    seed_connected_header(fixture, 0, 0, 0);
    // Refuse at 600 ticks already: a fabricated initial state could otherwise
    // be persisted on the next connect and mistaken for known state at 60.
    CHECK_FALSE(fixture.start());
    fixture.close();
    database::settings settings;
    settings.directory = fixture.dir();
    {
        database::data_base db(settings);
        REQUIRE(db.open());
        auto const stored = db.internal_db().get_header_and_abla_state(1);
        REQUIRE(stored.has_value());
        CHECK(std::get<2>(*stored) == 0);
        CHECK(std::get<3>(*stored) == 0);
        REQUIRE(db.close());
    }
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    fixture.settings().cantor_activation_time = 0;
#endif
    // Reaching the later upgrade cannot rescue the same unknown database.
    CHECK_FALSE(fixture.start());
}

TEST_CASE("a malformed persisted ABLA state refuses startup rather than resetting", "[abla][candidate_restore]") {
    chain_fixture fixture("abla_corrupt_parent");
    REQUIRE(fixture.created());
    // A known state's control value is below its configured floor.
    seed_connected_header(fixture, 64'000'000, 1, 32'000'000);
    CHECK_FALSE(fixture.start());
}

TEST_CASE("a candidate refuses ABLA metadata from a different branch at the same height", "[abla][candidate_restore]") {
    chain_fixture fixture("abla_wrong_parent");
    REQUIRE(fixture.created());
    seed_connected_header(fixture, 64'000'000, 32'000'000, 32'000'000);
    REQUIRE(fixture.start());
    auto const alternate = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 590, 2, {}, 0);
    auto& chain = fixture.chain();
    auto const inserted = chain.headers().add(alternate.hash(), alternate.header());
    REQUIRE(inserted.inserted);
    chain.headers().active_set_tip(inserted.index);
    CHECK(chain.publish_chain_view(1) == error::pool_state_failed);
}
#endif
