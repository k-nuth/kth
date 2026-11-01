// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <kth/infrastructure/utility/timer.hpp>
#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"
#if defined(KTH_CURRENCY_BCH)
using namespace kth;
using namespace kth::test;
TEST_CASE("re-persisting an identical header preserves its connected ABLA metadata", "[header_persistence][abla_rewrite]") {
    auto const index_state = GENERATE(0, 1, 2); // present, missing, wrong height
    chain_fixture fixture("header_abla_rewrite");
    REQUIRE(fixture.created());
    fixture.settings().cantor_activation_time = UINT64_MAX;
    auto const block = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 600, 1, {}, 0);
    {
        database::settings settings;
        settings.directory = fixture.dir();
        database::data_base db(settings);
        REQUIRE(db.open());
        REQUIRE_FALSE(db.push_header(block.header(), 1, block.serialized_size(), 32'000'000, 32'000'000));
        REQUIRE(db.internal_db().publish_transition(database::transition_heights{
            .last_block_height=1, .utxo_built_height=1}) == database::result_code::success);
        REQUIRE(db.close());
    }
    if (index_state != 0) {
        // Model a missing/stale secondary index while the authoritative row
        // remains intact. Open only while the fixture has no live database.
        struct environment {
            KTH_DB_env* env = nullptr;
            KTH_DB_txn* txn = nullptr;
            ~environment() {
                if (txn) kth_db_txn_abort(txn);
                if (env) kth_db_env_close(env);
            }
        } raw;
        REQUIRE(kth_db_env_create(&raw.env) == KTH_DB_SUCCESS);
        REQUIRE(kth_db_env_set_maxdbs(raw.env, 32) == KTH_DB_SUCCESS);
        database::store const store(fixture.dir());
        REQUIRE(kth_db_env_open(raw.env, store.internal_db_dir.string().c_str(), KTH_DB_NOTLS, 0664) == KTH_DB_SUCCESS);
        REQUIRE(kth_db_txn_begin(raw.env, nullptr, 0, &raw.txn) == KTH_DB_SUCCESS);
        KTH_DB_dbi index;
        REQUIRE(kth_db_dbi_open(raw.txn, "block_header_by_hash", 0, &index) == KTH_DB_SUCCESS);
        auto hash = block.hash();
        auto key = kth_db_make_value(hash.size(), hash.data());
        if (index_state == 1) {
            REQUIRE(kth_db_del(raw.txn, index, &key, nullptr) == KTH_DB_SUCCESS);
        } else {
            uint32_t wrong_height = 0;
            auto value = kth_db_make_value(sizeof(wrong_height), &wrong_height);
            REQUIRE(kth_db_put(raw.txn, index, &key, &value, 0) == KTH_DB_SUCCESS);
        }
        auto const committed = kth_db_txn_commit(raw.txn);
        raw.txn = nullptr;
        REQUIRE(committed == KTH_DB_SUCCESS);
    }
    fixture.settings().max_checkpoint_height = 1;
    REQUIRE(fixture.start());
    auto const before = fixture.chain().get_header_and_abla_state(1);
    REQUIRE(before);
    REQUIRE(std::get<1>(*before) == block.serialized_size());
    REQUIRE(std::get<2>(*before) == 32'000'000);
    REQUIRE(std::get<3>(*before) == 32'000'000);
    REQUIRE_FALSE(fixture.chain().organize_headers_batch({block.header()}, 1));
    auto const after = fixture.chain().get_header_and_abla_state(1);
    REQUIRE(after);
    CHECK(domain::chain::hash(std::get<0>(*after)) == domain::chain::hash(std::get<0>(*before)));
    CHECK(std::get<1>(*after) == std::get<1>(*before));
    CHECK(std::get<2>(*after) == std::get<2>(*before));
    CHECK(std::get<3>(*after) == std::get<3>(*before));
    fixture.close();
    database::settings settings;
    settings.directory = fixture.dir();
    database::data_base db(settings);
    REQUIRE(db.open());
    auto const indexed = db.internal_db().get_header(block.hash());
    REQUIRE(indexed);
    CHECK(indexed->second == 1);
    CHECK(domain::chain::hash(indexed->first) == block.hash());
    auto const persisted = db.internal_db().get_header_and_abla_state(1);
    REQUIRE(persisted);
    CHECK(*persisted == *before);
    REQUIRE(db.close());
}

TEST_CASE("persisting a different header replaces its row and hash index", "[header_persistence][abla_rewrite]") {
    chain_fixture fixture("header_abla_replacement");
    REQUIRE(fixture.created());
    auto const original = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 600, 1, {}, 0);
    auto const replacement = mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        original.header().timestamp() + 1, 2, {}, 0);
    REQUIRE(original.hash() != replacement.hash());
    database::settings settings;
    settings.directory = fixture.dir();
    database::data_base db(settings);
    REQUIRE(db.open());
    REQUIRE_FALSE(db.push_header(original.header(), 1, original.serialized_size(), 32'000'000, 32'000'000));
    REQUIRE_FALSE(db.push_headers_batch({replacement.header()}, 1));
    auto const row = db.internal_db().get_header_and_abla_state(1);
    REQUIRE(row);
    CHECK(domain::chain::hash(std::get<0>(*row)) == replacement.hash());
    CHECK(std::get<1>(*row) == 0);
    CHECK(std::get<2>(*row) == 0);
    CHECK(std::get<3>(*row) == 0);
    auto const displaced = db.internal_db().get_header(original.hash());
    CHECK_FALSE(displaced.has_value());
    if (!displaced) CHECK(displaced.error() == database::result_code::key_not_found);
    auto const indexed = db.internal_db().get_header(replacement.hash());
    REQUIRE(indexed);
    CHECK(indexed->second == 1);
    CHECK(domain::chain::hash(indexed->first) == replacement.hash());
    REQUIRE(db.close());
}
#endif
