// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/node.hpp>

#include "sync_harness.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// Full historical validation is a mode, not a lowered checkpoint
// =============================================================================
//
// A checkpoint is two things at once in the optimized sync: an ANCHOR (a hash
// the header chain must pass through) and PERMISSION (everything at or below the
// highest one is connected merkle-only, with no script, signature or UTXO check
// and no undo). `chain.full_validation` withdraws the permission and keeps the
// anchors.
//
// Every scenario drives the node's own connect path (block_storage_task, then
// utxo_build_task, which runs the batch validator) over a regtest chain whose
// anchor is NOT zero, and the full-mode settings come out of the real option
// parser. Nothing here is compiled with KTH_TEST_LOW_CHECKPOINT.

namespace {

constexpr uint32_t trunk_len = 110;
constexpr uint32_t anchor_height = 105;   // nonzero, above the spend below
constexpr uint32_t spend_height = 101;    // first height block 1's coinbase has matured

struct scenario {
    std::vector<domain::chain::block> blocks;   // heights 1..trunk_len
    hash_digest anchor_hash;                    // the hash at anchor_height
};

// Heights 1..trunk_len. The block at spend_height spends block 1's coinbase; when
// `tampered` the output value is changed AFTER signing, so the signature no longer
// commits to what the transaction pays and the script must fail. `salt` makes the
// chain distinct from another built the same way.
scenario build_scenario(bool tampered, uint32_t salt) {
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

    scenario sc;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= trunk_len; ++h) {
        std::vector<domain::chain::transaction> txs;
        uint64_t fees = 0;
        if (h == spend_height) {
            auto const& coinbase = sc.blocks.front().transactions().front();
            constexpr uint64_t fee = 1000;
            auto const value = coinbase.outputs()[0].value() - fee;
            auto spend = spend_p2pkh(coinbase, 0, value, flags);
            if (tampered) {
                domain::chain::output::list outs;
                outs.emplace_back(value - 1, miner_script(), domain::chain::token_data_opt{});
                spend = domain::chain::transaction(1, 0, spend.inputs(), std::move(outs));
                fees = fee + 1;
            } else {
                fees = fee;
            }
            txs.push_back(std::move(spend));
        }
        sc.blocks.push_back(mine_block(prev, h, base_time + h * block_spacing, salt, std::move(txs), fees));
        prev = sc.blocks.back().hash();
    }
    sc.anchor_hash = sc.blocks[anchor_height - 1].hash();
    return sc;
}

// The real option parser, regtest, with `extra` after the network.
bool parse_options(node::parser& parser, std::vector<std::string> const& extra, std::ostringstream& error) {
    std::vector<std::string> args{"kth", "--network", "regtest"};
    args.insert(args.end(), extra.begin(), extra.end());
    std::vector<char const*> argv;
    for (auto const& a : args) argv.push_back(a.c_str());
    return parser.parse(int(argv.size()), argv.data(), error);
}

std::string anchor_option(scenario const& sc) {
    return "--chain.checkpoint=" + encode_hash(sc.anchor_hash) + ":" + std::to_string(anchor_height);
}

// What the optimized mode does for the same anchor, built from the public fields
// the settings constructor fills: the anchor is in the list, in the sorted copy,
// and is the maximum.
blockchain::settings optimized_settings(scenario const& sc) {
    blockchain::settings s(domain::config::network::regtest);
    s.checkpoints.emplace_back(sc.anchor_hash, anchor_height);
    s.checkpoints_sorted = s.checkpoints;
    std::ranges::sort(s.checkpoints_sorted);
    s.max_checkpoint_height = anchor_height;
    return s;
}

struct outcome {
    uint32_t built;
    bool undo_first;     // height 1
    bool undo_spend;     // spend_height
    bool undo_above;     // anchor_height + 1
};

// Headers into the index, then the node's connect path over the bodies.
outcome connect_under(char const* tag, blockchain::settings s, scenario const& sc) {
    chain_fixture fixture(tag, std::move(s));
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    auto const added = fixture.organizer().add_headers(headers_of(sc.blocks));
    REQUIRE( ! added.error);
    REQUIRE(added.headers_added == trunk_len);
    persist_headers(fixture, sc.blocks, 1);
    run_connect_tasks(fixture, sc.blocks, 1);

    auto const built = chain.get_utxo_built_height();
    auto const undo = [&](uint32_t h) {
        return chain.headers().has_undo_data(chain.headers().active_at(int32_t(h)));
    };
    return outcome{built ? *built : 0u, undo(1), undo(spend_height), undo(anchor_height + 1)};
}

} // namespace

// -----------------------------------------------------------------------------
// The optimized mode is what it was: the anchor grants permission
// -----------------------------------------------------------------------------

TEST_CASE("optimized mode connects a block with an invalid signature below the anchor",
          "[node][full_validation][optimized]") {
    // The behaviour full mode exists to withdraw, shown on the same chain. If
    // this failed, the full-mode rejection below could be a broken chain rather
    // than the mode.
    auto const sc = build_scenario(/*tampered*/ true, 0);
    auto const out = connect_under("fv_opt_bad", optimized_settings(sc), sc);

    CHECK(out.built == trunk_len);       // merkle-only through the anchor, then validated above it
    CHECK_FALSE(out.undo_first);         // no undo is captured at or below the anchor
    CHECK_FALSE(out.undo_spend);
    CHECK(out.undo_above);
}

TEST_CASE("optimized mode connects the valid control the same way",
          "[node][full_validation][optimized]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);
    auto const out = connect_under("fv_opt_ok", optimized_settings(sc), sc);

    CHECK(out.built == trunk_len);
    CHECK_FALSE(out.undo_first);
    CHECK_FALSE(out.undo_spend);
    CHECK(out.undo_above);
}

TEST_CASE("the optimized default derives the checkpoint list and its maximum unchanged",
          "[node][full_validation][optimized][parser]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "mainnet"};
    std::ostringstream error;
    REQUIRE(parser.parse(3, argv, error));

    blockchain::settings const reference(domain::config::network::mainnet);
    auto const& chain = parser.configured.chain;
    CHECK(chain.checkpoints == reference.checkpoints);
    CHECK(chain.checkpoints_sorted == reference.checkpoints_sorted);
    CHECK(chain.max_checkpoint_height == reference.max_checkpoint_height);
    CHECK(chain.max_checkpoint_height != 0);
    CHECK_FALSE(chain.full_validation);
    CHECK(chain.trusted_checkpoint_height() == chain.max_checkpoint_height);
}

// -----------------------------------------------------------------------------
// Full mode: anchors kept, permission withdrawn
// -----------------------------------------------------------------------------

TEST_CASE("full mode keeps the exact built-in checkpoint list and maximum",
          "[node][full_validation][parser]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "mainnet", "--chain.full_validation=true"};
    std::ostringstream error;
    REQUIRE(parser.parse(4, argv, error));

    blockchain::settings const reference(domain::config::network::mainnet);
    auto const& chain = parser.configured.chain;
    CHECK(chain.checkpoints == reference.checkpoints);
    CHECK(chain.checkpoints_sorted == reference.checkpoints_sorted);
    CHECK(chain.max_checkpoint_height == reference.max_checkpoint_height);
    CHECK(chain.max_checkpoint_height != 0);
    CHECK(chain.full_validation);
    CHECK(chain.trusted_checkpoint_height() == 0);   // the anchor is kept; the permission is not
}

TEST_CASE("full mode keeps a configured nonzero anchor in the list, the sorted copy and the maximum",
          "[node][full_validation][parser]") {
    auto const sc = build_scenario(false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    auto const& chain = parser.configured.chain;
    auto const has_anchor = [&](auto const& list) {
        return std::ranges::any_of(list, [&](auto const& c) {
            return c.hash() == sc.anchor_hash && c.height() == anchor_height;
        });
    };
    CHECK(has_anchor(chain.checkpoints));
    CHECK(has_anchor(chain.checkpoints_sorted));
    CHECK(chain.max_checkpoint_height == anchor_height);
    CHECK(chain.trusted_checkpoint_height() == 0);
}

TEST_CASE("full mode rejects an invalid signature below a nonzero anchor",
          "[node][full_validation]") {
    auto const sc = build_scenario(/*tampered*/ true, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    // Batches of 100: blocks 1..100 are one batch and the spend at 101 opens the
    // next, so the valid blocks connect before the invalid one is judged. With
    // one batch for all of them, none would connect: a rejected batch applies
    // nothing.
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc),
        "--chain.validation_batch_blocks=100"}, error));
    REQUIRE(parser.configured.chain.max_checkpoint_height == anchor_height);

    auto const out = connect_under("fv_full_bad", parser.configured.chain, sc);

    // Blocks 1..spend_height-1 were validated and connected; the batch holding the
    // spend failed script validation and nothing of it was applied.
    CHECK(out.built == spend_height - 1);
    CHECK(out.undo_first);

    // That it is the SCRIPT that fails, not something about the chain: the batch
    // validator on the same blocks gives the script verdict.
    chain_fixture fixture("fv_full_bad_reason", parser.configured.chain);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE( ! fixture.organizer().add_headers(headers_of(sc.blocks)).error);
    std::vector<domain::message::block::const_ptr> messages;
    for (auto const& blk : sc.blocks) {
        messages.push_back(std::make_shared<domain::message::block const>(blk));
    }
    auto const reason = blockchain::validate_block_batch(fixture.chain(),
        parser.configured.chain, domain::config::network::regtest, messages, 1);
#if defined(WITH_CONSENSUS)
    // The consensus library reports every script failure as invalid_script.
    CHECK(reason == error::invalid_script);
#else
    CHECK(reason == error::sig_nullfail);
#endif
}

TEST_CASE("full mode accepts the valid control and captures undo from the first block",
          "[node][full_validation]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));
    REQUIRE(parser.configured.chain.max_checkpoint_height == anchor_height);

    auto const out = connect_under("fv_full_ok", parser.configured.chain, sc);

    CHECK(out.built == trunk_len);
    CHECK(out.undo_first);       // below the anchor, which optimized mode never captures
    CHECK(out.undo_spend);
    CHECK(out.undo_above);
}

TEST_CASE("full mode still rejects a header that conflicts with the configured anchor",
          "[node][full_validation]") {
    auto const sc = build_scenario(false, 0);
    auto const other = build_scenario(false, 1);   // same heights, different hashes
    REQUIRE(other.anchor_hash != sc.anchor_hash);

    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    chain_fixture fixture("fv_full_conflict", parser.configured.chain);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const added = fixture.organizer().add_headers(headers_of(other.blocks));
    CHECK(added.error == error::checkpoints_failed);
    CHECK(added.headers_added < anchor_height);
}

// -----------------------------------------------------------------------------
// The option reaches the parser the way the others do
// -----------------------------------------------------------------------------

TEST_CASE("full_validation is read from a configuration file like the other chain options",
          "[node][full_validation][parser]") {
    auto const path = std::filesystem::temp_directory_path() /
        ("kth_full_validation_" + std::to_string(getpid()) + ".cfg");
    {
        std::ofstream out(path);
        out << "chain.full_validation = true\n";
    }
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    auto const parsed = parser.parse_from_file(path, error);
    std::filesystem::remove(path);
    REQUIRE(parsed);
    CHECK(parser.configured.chain.full_validation);
}

TEST_CASE("full mode restarts on undo coverage that starts at the first block",
          "[node][full_validation]") {
    // The startup coverage check demands undo for every connected height above the
    // permission boundary. In full mode that is height 1, and it is the anchor + 1
    // only if the check still reads the anchor: this chain has no undo above 105
    // that would make the two differ, so the restart is what tells them apart.
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    chain_fixture fixture("fv_full_restart", parser.configured.chain);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE( ! fixture.organizer().add_headers(headers_of(sc.blocks)).error);
    persist_headers(fixture, sc.blocks, 1);
    run_connect_tasks(fixture, sc.blocks, 1);
    REQUIRE(fixture.chain().get_utxo_built_height().value_or(0) == trunk_len);

    REQUIRE(fixture.restart());
    CHECK(fixture.chain().get_utxo_built_height().value_or(0) == trunk_len);
    CHECK(fixture.chain().headers().has_undo_data(fixture.chain().headers().active_at(1)));
}

TEST_CASE("full mode refuses to start on a database connected without undo below the anchor",
          "[node][full_validation]") {
    // Connected in the optimized mode, so blocks 1..anchor carry no undo. Opening
    // the same database in full mode claims those heights are covered, which is
    // false, and the startup coverage check has to say so rather than let a later
    // reorganization find the gap. Keyed to the anchor instead of the permission
    // boundary the check would see only the heights above it and pass.
    auto const sc = build_scenario(/*tampered*/ false, 0);
    chain_fixture fixture("fv_mode_switch", optimized_settings(sc));
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE( ! fixture.organizer().add_headers(headers_of(sc.blocks)).error);
    persist_headers(fixture, sc.blocks, 1);
    run_connect_tasks(fixture, sc.blocks, 1);
    REQUIRE(fixture.chain().get_utxo_built_height().value_or(0) == trunk_len);
    REQUIRE_FALSE(fixture.chain().headers().has_undo_data(fixture.chain().headers().active_at(1)));

    // The control: the optimized mode reopens the same database.
    REQUIRE(fixture.restart());

    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));
    CHECK_FALSE(fixture.restart_with(parser.configured.chain));
}

// Full mode retains anchors but cannot use them to bypass context-free or
// contextual header checks. Each synthetic chain agrees with its anchor, so
// only the validation policy distinguishes the invalid first header.
TEST_CASE("full mode checks header work version and time below a retained anchor",
          "[node][full_validation][header_checks]") {
    auto sc = build_scenario(false, 0);
    enum class fault { work, version, time } selected = fault::work;
    SECTION("proof of work") { selected = fault::work; }
    SECTION("minimum version") { selected = fault::version; }
    SECTION("median time past") { selected = fault::time; }
    auto const genesis = domain::chain::block::genesis_regtest();
    auto previous = genesis.hash();
    for (size_t i = 0; i < sc.blocks.size(); ++i) {
        auto const original = sc.blocks[i];
        auto version = original.header().version();
        auto timestamp = original.header().timestamp();
        if (i == 0 && selected == fault::version) version = 0;
        if (i == 0 && selected == fault::time) timestamp = genesis.header().timestamp();
        bool found = false;
        for (uint64_t nonce = 0; nonce <= max_uint32; ++nonce) {
            domain::chain::header header{version, previous, original.generate_merkle_root(),
                timestamp, original.header().bits(), uint32_t(nonce)};
            bool const valid = header.is_valid_proof_of_work(domain::chain::hash(header), false);
            if (valid != !(i == 0 && selected == fault::work)) continue;
            sc.blocks[i] = domain::chain::block(std::move(header),
                domain::chain::transaction::list(original.transactions()));
            previous = sc.blocks[i].hash();
            found = true;
            break;
        }
        REQUIRE(found);
    }
    sc.anchor_hash = sc.blocks[anchor_height - 1].hash();
    node::parser parser(domain::config::network::regtest);
    std::ostringstream errors;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, errors));
    REQUIRE(parser.configured.chain.max_checkpoint_height == anchor_height);
    chain_fixture fixture("fv_header_checks", parser.configured.chain);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const added = fixture.organizer().add_headers(headers_of(sc.blocks));
    auto const expected = selected == fault::work
        ? sc.blocks.front().header().check(sc.blocks.front().hash(), false)
        : selected == fault::version ? code(error::old_version_block) : code(error::timestamp_too_early);
    REQUIRE(expected != error::success);
    CHECK(added.error == expected);
    CHECK(added.headers_added == 0);
}
