// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <utxoz/utils.hpp>

#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/domain/chain/script_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/node.hpp>
#include <kth/node/detail/compact_filters.hpp>
#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/dispatch.hpp>

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

TEST_CASE("full mode connects a chain cut into many batches exactly as it connects it in one",
          "[node][full_validation]") {
    // Batches of ten: eleven of them over this chain, and every one after the
    // first is judged while the one before it is still being made durable and
    // published. What is connected must not depend on that — the same height,
    // and undo for the same blocks, as the single batch of the control above.
    // The spend at 101 opens a batch whose input was created by a much earlier
    // one, so a judgement that ran against a set missing an applied batch would
    // not resolve it.
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc),
        "--chain.validation_batch_blocks=10"}, error));

    auto const out = connect_under("fv_full_many_batches", parser.configured.chain, sc);

    CHECK(out.built == trunk_len);
    CHECK(out.undo_first);
    CHECK(out.undo_spend);
    CHECK(out.undo_above);
}

TEST_CASE("full mode judges a batch ahead of its turn and still refuses it in its turn",
          "[node][full_validation]") {
    // The tampered spend at 101 with batches of ten: 1..100 are ten valid
    // batches and the eleventh holds the invalid block. Its verdict is reached
    // while the tenth is being finished, and must change nothing about the
    // tenth: every block before the invalid one is connected, and nothing of
    // the batch that holds it is.
    auto const sc = build_scenario(/*tampered*/ true, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc),
        "--chain.validation_batch_blocks=10"}, error));

    auto const out = connect_under("fv_full_bad_ahead", parser.configured.chain, sc);

    CHECK(out.built == spend_height - 1);
    CHECK(out.undo_first);
    CHECK_FALSE(out.undo_spend);
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

// -----------------------------------------------------------------------------
// The outputs a block spends, and the fee getblock reports from them
// -----------------------------------------------------------------------------

namespace {

template <typename Awaitable>
auto run_on_own_context(Awaitable&& operation) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io, std::forward<Awaitable>(operation), ::asio::use_future);
    io.run();
    return result.get();
}

// Headers into the index, then the node's connect path over the bodies, on a
// fixture the caller keeps to read the chain afterwards.
void connect_on(chain_fixture& fixture, scenario const& sc) {
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE( ! fixture.organizer().add_headers(headers_of(sc.blocks)).error);
    persist_headers(fixture, sc.blocks, 1);
    run_connect_tasks(fixture, sc.blocks, 1);
}

// The JSON-RPC reply to getblock <hash> <verbosity>.
std::string get_block_reply(chain_fixture& fixture, hash_digest const& hash, int verbosity) {
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{fixture.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto const body = R"({"method":"getblock","params":[")" + encode_hash(hash) + R"(",)"
        + std::to_string(verbosity) + R"(],"id":7})";
    return run_on_own_context(dispatcher.handle(context, body));
}

} // namespace

TEST_CASE("the outputs a block spends are read from its undo record, and getblock reports the fee",
          "[node][full_validation][spent_outputs]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    chain_fixture fixture("fv_spent_outputs", parser.configured.chain);
    connect_on(fixture, sc);
    auto const& chain = fixture.chain();

    // The one spend of the chain: block 1's coinbase output, created at height 1.
    auto const& spending = sc.blocks[spend_height - 1];
    auto const stored = run_on_own_context(chain.fetch_block(spending.hash()));
    REQUIRE(stored);
    auto const spent = run_on_own_context(chain.fetch_spent_outputs(spending.hash(), stored->first));
    REQUIRE(spent);
    REQUIRE(spent->outputs.size() == 1);
    CHECK(spent->outputs.front() == sc.blocks.front().transactions().front().outputs()[0]);
    CHECK(spent->heights == std::vector<uint32_t>{1});

    // A block of one transaction spends nothing, and no block is no outputs.
    auto const& plain = sc.blocks[4];
    auto const plain_stored = run_on_own_context(chain.fetch_block(plain.hash()));
    REQUIRE(plain_stored);
    auto const none = run_on_own_context(chain.fetch_spent_outputs(plain.hash(), plain_stored->first));
    REQUIRE(none);
    CHECK(none->outputs.empty());
    auto const missing = run_on_own_context(chain.fetch_spent_outputs(spending.hash(), nullptr));
    REQUIRE( ! missing);
    CHECK(missing.error() == error::not_found);

    // getblock with the transactions decoded gives the spend its fee, 1,000
    // satoshis, after its outputs. The coinbase has none, and neither has the
    // list of txids.
    auto const decoded = get_block_reply(fixture, spending.hash(), 2);
    auto const at = decoded.find(R"(}],"fee":0.00001,"hex":")");
    REQUIRE(at != std::string::npos);
    CHECK(decoded.find(R"("fee":)") == at + 3);
    CHECK(decoded.find(R"("fee":)", at + 4) == std::string::npos);
    CHECK(get_block_reply(fixture, spending.hash(), 1).find(R"("fee":)") == std::string::npos);
    CHECK(get_block_reply(fixture, plain.hash(), 2).find(R"("fee":)") == std::string::npos);
}

TEST_CASE("a block connected without an undo record is decoded without fees",
          "[node][full_validation][optimized][spent_outputs]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);

    chain_fixture fixture("fv_spent_outputs_opt", optimized_settings(sc));
    connect_on(fixture, sc);
    auto const& chain = fixture.chain();

    // Below the anchor the node wrote no record of what the block spent.
    auto const& spending = sc.blocks[spend_height - 1];
    auto const stored = run_on_own_context(chain.fetch_block(spending.hash()));
    REQUIRE(stored);
    auto const spent = run_on_own_context(chain.fetch_spent_outputs(spending.hash(), stored->first));
    REQUIRE( ! spent);
    CHECK(spent.error() == error::not_found);

    // getblock still decodes both transactions; the fee field is left out.
    auto const decoded = get_block_reply(fixture, spending.hash(), 2);
    CHECK(decoded.find(R"("error":null)") != std::string::npos);
    CHECK(decoded.find(encode_hash(spending.transactions()[1].hash())) != std::string::npos);
    CHECK(decoded.find(R"("fee":)") == std::string::npos);
}

// -----------------------------------------------------------------------------
// Script filters served to light wallets: filters, filter headers, ranges, proofs
// -----------------------------------------------------------------------------
//
// The script filter of a block lists the scripts of the outputs the block
// spends. The node keeps those already, in the undo record it writes when it
// connects a block, so the filter is built from the two stored records and
// nothing is stored for it.

namespace {

// The filter the builder gives for the block at `h` and the outputs it spends.
// The one spend of the chain is of the coinbase output of block 1.
domain::chain::script_filter expected_filter(scenario const& sc, uint32_t h) {
    std::vector<domain::chain::output> spent;
    if (h == spend_height) {
        spent.push_back(sc.blocks.front().transactions().front().outputs()[0]);
    }
    auto const& block = sc.blocks[h - 1];
    auto filter = domain::chain::build_script_filter(block, spent);
    REQUIRE(filter);
    return std::move(*filter);
}

using node::detail::compact_filter_service;

constexpr size_t filter_interval = 4;
constexpr auto script_type = domain::message::script_compact_filter;
constexpr auto regular_type = domain::message::regular_compact_filter;

// The files a service keeps its filter header records in, removed with it.
struct filter_files {
    std::filesystem::path regular;
    std::filesystem::path script;

    explicit filter_files(std::string const& tag)
        : regular(std::filesystem::temp_directory_path() / ("kth_" + tag + "_regular_checkpoints"))
        , script(std::filesystem::temp_directory_path() / ("kth_" + tag + "_script_checkpoints"))
    {
        std::filesystem::remove(regular);
        std::filesystem::remove(script);
    }

    ~filter_files() {
        std::error_code ignored;
        std::filesystem::remove(regular, ignored);
        std::filesystem::remove(script, ignored);
    }
};

// The script filter of a block of the scenario as it is served: the root of
// its entry tree, then the set.
data_chunk served_script_filter(scenario const& sc, uint32_t h) {
    if (h == 0) {
        auto const filter = domain::chain::build_script_filter(domain::chain::block::genesis_regtest(), {});
        REQUIRE(filter);
        return domain::chain::to_data(*filter);
    }
    return domain::chain::to_data(expected_filter(sc, h));
}

// The definition, spelled out: all zeros before the first block.
hash_digest script_filter_header_at(scenario const& sc, uint32_t height) {
    hash_digest header = null_hash;
    for (uint32_t h = 0; h <= height; ++h) {
        header = bitcoin_hash(splice(bitcoin_hash(served_script_filter(sc, h)), header));
    }
    return header;
}

hash_digest block_hash_at(scenario const& sc, uint32_t h) {
    return h == 0 ? domain::chain::block::genesis_regtest().hash() : sc.blocks[h - 1].hash();
}

domain::message::get_compact_filter_range range_request(hash_digest const& block, hash_digest const& prefix,
                                                        uint16_t bits) {
    return {script_type, block, bits, prefix};
}

// Checks a cfrange against the root in front of the block's filter, as a
// wallet does.
std::expected<void, domain::chain::script_range_error>
checked(domain::message::compact_filter_range const& answer, hash_digest const& root, hash_digest const& prefix,
        uint16_t bits) {
    return domain::chain::check_script_range(root, answer.leaf_count, answer.first_index, answer.leaves,
        answer.before ? &*answer.before : nullptr, answer.after ? &*answer.after : nullptr,
        answer.hashes, prefix, bits);
}

} // namespace

TEST_CASE("script filters are served with their headers from the first block when every block has its undo record",
          "[node][full_validation][script_filters]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    chain_fixture fixture("fv_filters_served", parser.configured.chain);
    connect_on(fixture, sc);
    filter_files const files("fv_filters_served");
    compact_filter_service service(fixture.chain(), domain::chain::block::genesis_regtest(), files.regular,
        filter_interval);

    // Until its file is named, only the regular filter is served.
    CHECK(service.serves(regular_type));
    CHECK( ! service.serves(script_type));
    CHECK( ! run_on_own_context(service.filter_of(block_hash_at(sc, 1), script_type)));
    service.serve_script_filters(files.script);
    CHECK(service.serves(script_type));
    CHECK( ! service.serves(2));
    service.load();

    // The filter of a block is the builder's, with the root in front, whether
    // the block spends nothing or spends an output of another block.
    for (uint32_t h : {0u, 1u, spend_height - 1, spend_height, trunk_len}) {
        CAPTURE(h);
        auto const filter = run_on_own_context(service.filter_of(block_hash_at(sc, h), script_type));
        REQUIRE(filter);
        REQUIRE(*filter);
        CHECK(**filter == served_script_filter(sc, h));
        // And it is not the regular filter of the same block.
        auto const regular = run_on_own_context(service.filter_of(block_hash_at(sc, h)));
        REQUIRE(regular);
        CHECK(**regular != **filter);
    }
    // The builder refuses the spending block without the output it spends, so
    // its filter came from the undo record and not from the block alone.
    CHECK( ! domain::chain::build_script_filter(sc.blocks[spend_height - 1], {}));

    // One record for every `filter_interval` blocks, for each filter type, in
    // a file of its own.
    run_on_own_context(service.extend());
    auto const records = service.checkpoints(script_type);
    REQUIRE(records.size() == trunk_len / filter_interval);
    REQUIRE(service.checkpoints(regular_type).size() == records.size());
    CHECK(records.front().block == block_hash_at(sc, filter_interval));
    CHECK(records.front().filter_header == script_filter_header_at(sc, filter_interval));
    CHECK(records.front().filter_header != service.checkpoints(regular_type).front().filter_header);
    CHECK(records.back().block == block_hash_at(sc, 108));
    CHECK(records.back().filter_header == script_filter_header_at(sc, 108));
    CHECK(std::filesystem::file_size(files.script) == records.size() * 64u);
    CHECK(std::filesystem::file_size(files.regular) == records.size() * 64u);

    // cfcheckpt and cfheaders for the script filter, across the spending block.
    auto const checkpoint = service.checkpoint({script_type, block_hash_at(sc, trunk_len)});
    REQUIRE(checkpoint);
    CHECK(checkpoint->filter_type == script_type);
    REQUIRE(checkpoint->filter_headers.size() == records.size());
    CHECK(checkpoint->filter_headers[24] == script_filter_header_at(sc, 100));

    auto const headers = run_on_own_context(service.headers({script_type, spend_height,
        block_hash_at(sc, spend_height + 1)}));
    REQUIRE(headers);
    CHECK(headers->filter_type == script_type);
    CHECK(headers->previous_filter_header == script_filter_header_at(sc, spend_height - 1));
    REQUIRE(headers->filter_hashes.size() == 2);
    CHECK(headers->filter_hashes[0] == bitcoin_hash(served_script_filter(sc, spend_height)));
    CHECK(headers->filter_hashes[1] == bitcoin_hash(served_script_filter(sc, spend_height + 1)));

    // Another run of the node reads both files.
    compact_filter_service reopened(fixture.chain(), domain::chain::block::genesis_regtest(), files.regular,
        filter_interval);
    reopened.serve_script_filters(files.script);
    reopened.load();
    CHECK(reopened.checkpoints(script_type) == records);
    CHECK(reopened.checkpoints(regular_type) == service.checkpoints(regular_type));
}

TEST_CASE("a cfrange and a cfproof of the spending block check against the root in front of its filter",
          "[node][full_validation][script_filters]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parse_options(parser, {"--chain.full_validation=true", anchor_option(sc)}, error));

    chain_fixture fixture("fv_filters_tree", parser.configured.chain);
    connect_on(fixture, sc);
    filter_files const files("fv_filters_tree");
    compact_filter_service service(fixture.chain(), domain::chain::block::genesis_regtest(), files.regular,
        filter_interval);
    service.serve_script_filters(files.script);

    auto const& spending_block = sc.blocks[spend_height - 1];
    auto const& created_block = sc.blocks.front();
    auto const block = spending_block.hash();
    auto const root = expected_filter(sc, spend_height).root;
    auto const& spent_output = created_block.transactions().front().outputs()[0];
    auto const spent_key = domain::chain::script_key(spent_output.script().bytes());

    // Every leaf: no bits of prefix, no neighbour, no hash.
    auto const all = run_on_own_context(service.range(range_request(block, null_hash, 0)));
    REQUIRE(all);
    CHECK(all->filter_type == script_type);
    CHECK(all->block_hash == block);
    CHECK(all->first_index == 0);
    REQUIRE(all->leaf_count == all->leaves.size());
    REQUIRE(all->leaves.size() >= 3);   // the coinbase output, the spend's output and the output it spends
    CHECK( ! all->before);
    CHECK( ! all->after);
    CHECK(all->hashes.empty());
    CHECK(checked(*all, root, null_hash, 0));
    CHECK(std::ranges::is_sorted(all->leaves));
    CHECK(domain::chain::script_leaves_root(all->leaves) == root);

    // The key of the script the block spends, whole: its leaves and nothing else.
    auto const exact = run_on_own_context(service.range(range_request(block, spent_key, 256)));
    REQUIRE(exact);
    REQUIRE( ! exact->leaves.empty());
    for (auto const& leaf : exact->leaves) CHECK(leaf.key == spent_key);
    CHECK(std::ranges::any_of(exact->leaves, [](auto const& leaf) {
        return leaf.kind == domain::chain::script_leaf_kind::spent;
    }));
    CHECK(checked(*exact, root, spent_key, 256));
    // The same answer does not pass for another root, or for another prefix.
    CHECK( ! checked(*exact, null_hash, spent_key, 256));
    hash_digest other_key = spent_key;
    other_key[0] ^= 0x80;
    CHECK( ! checked(*exact, root, other_key, 256));

    // A key the block does not have: no leaf, and the neighbours show the gap.
    auto const none = run_on_own_context(service.range(range_request(block, other_key, 256)));
    REQUIRE(none);
    CHECK(none->leaves.empty());
    CHECK(none->leaf_count == all->leaf_count);
    CHECK(checked(*none, root, other_key, 256));

    // What is not answered: a prefix with a bit past its length, the regular
    // filter, which has no entry tree, and a block the node does not know.
    hash_digest loose{};
    loose[1] = 0x01;
    CHECK( ! run_on_own_context(service.range(range_request(block, loose, 8))));
    auto regular = range_request(block, null_hash, 0);
    regular.filter_type = regular_type;
    CHECK( ! run_on_own_context(service.range(regular)));
    hash_digest unknown{};
    unknown[0] = 0x42;
    CHECK( ! run_on_own_context(service.range(range_request(unknown, null_hash, 0))));

    // The proof of the spent output's leaf: the leaf folds to the root, its
    // transaction to this block's header, and the transaction that created
    // the output to the header of the block it is in.
    auto const spent_at = std::ranges::find_if(all->leaves, [](auto const& leaf) {
        return leaf.kind == domain::chain::script_leaf_kind::spent;
    });
    REQUIRE(spent_at != all->leaves.end());
    auto const spent_index = uint32_t(spent_at - all->leaves.begin());
    auto const proof = run_on_own_context(service.proof({script_type, block, spent_index}));
    REQUIRE(proof);
    CHECK(proof->leaf_count == all->leaf_count);
    CHECK(proof->leaf_index == spent_index);
    CHECK(proof->leaf == *spent_at);
    CHECK(proof->leaf.key == spent_key);
    auto const rebuilt = domain::chain::script_range_root(proof->leaf_count, proof->leaf_index,
        std::span<domain::chain::script_leaf const>(&proof->leaf, 1), proof->leaf_branch);
    REQUIRE(rebuilt);
    CHECK(*rebuilt == root);
    REQUIRE(proof->leaf.tx_index == 1);
    CHECK(proof->transaction == spending_block.transactions()[1]);
    CHECK(domain::chain::block::merkle_branch_root(proof->transaction.hash(), proof->leaf.tx_index,
        proof->transaction_branch) == spending_block.header().merkle());
    REQUIRE(proof->origin);
    CHECK(proof->origin->block_hash == created_block.hash());
    CHECK(proof->origin->transaction_index == 0);
    CHECK(proof->origin->transaction == created_block.transactions().front());
    CHECK(proof->origin->transaction_branch.empty());
    CHECK(domain::chain::block::merkle_branch_root(proof->origin->transaction.hash(),
        proof->origin->transaction_index, proof->origin->transaction_branch) == created_block.header().merkle());
    // The output the input names pays to the script the leaf's key is of.
    auto const& point = proof->transaction.inputs()[proof->leaf.index].previous_output();
    CHECK(point.hash() == proof->origin->transaction.hash());
    CHECK(domain::chain::script_key(proof->origin->transaction.outputs()[point.index()].script().bytes())
        == proof->leaf.key);
    // It goes on the wire and comes back whole.
    auto const raw = to_data_chunk(*proof, domain::message::version::level::maximum);
    auto const again = from_data_chunk<domain::message::compact_filter_proof>(raw,
        domain::message::version::level::maximum);
    REQUIRE(again);
    CHECK(*again == *proof);

    // The proof of a created output's leaf carries no other transaction.
    auto const created_at = std::ranges::find_if(all->leaves, [](auto const& leaf) {
        return leaf.kind == domain::chain::script_leaf_kind::output && leaf.tx_index == 0;
    });
    REQUIRE(created_at != all->leaves.end());
    auto const created_index = uint32_t(created_at - all->leaves.begin());
    auto const created = run_on_own_context(service.proof({script_type, block, created_index}));
    REQUIRE(created);
    CHECK( ! created->origin);
    CHECK(created->transaction == spending_block.transactions().front());
    CHECK(domain::chain::block::merkle_branch_root(created->transaction.hash(), 0, created->transaction_branch)
        == spending_block.header().merkle());
    auto const created_root = domain::chain::script_range_root(created->leaf_count, created->leaf_index,
        std::span<domain::chain::script_leaf const>(&created->leaf, 1), created->leaf_branch);
    REQUIRE(created_root);
    CHECK(*created_root == root);

    // A leaf past the last one, and the regular filter, are not answered.
    CHECK( ! run_on_own_context(service.proof({script_type, block, all->leaf_count})));
    CHECK( ! run_on_own_context(service.proof({regular_type, block, 0})));
    CHECK( ! run_on_own_context(service.proof({script_type, unknown, 0})));

    // The first block has an entry tree too, and spends nothing.
    auto const first = run_on_own_context(service.range(range_request(block_hash_at(sc, 0), null_hash, 0)));
    REQUIRE(first);
    CHECK(first->leaf_count == first->leaves.size());
}

TEST_CASE("script filter headers stop at the first block connected without the outputs it spends",
          "[node][full_validation][optimized][script_filters]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);

    chain_fixture fixture("fv_filters_served_opt", optimized_settings(sc));
    connect_on(fixture, sc);
    filter_files const files("fv_filters_served_opt");
    compact_filter_service service(fixture.chain(), domain::chain::block::genesis_regtest(), files.regular,
        filter_interval);
    service.serve_script_filters(files.script);
    service.load();

    // The spending block is below the anchor and has no undo record. Every
    // block before it spends nothing, so the records reach height 100 and no
    // further; the regular filter needs the blocks alone and reaches the tip.
    run_on_own_context(service.extend());
    auto const records = service.checkpoints(script_type);
    REQUIRE(records.size() == (spend_height - 1) / filter_interval);
    CHECK(records.back().block == block_hash_at(sc, 100));
    CHECK(records.back().filter_header == script_filter_header_at(sc, 100));
    CHECK(service.checkpoints(regular_type).size() == trunk_len / filter_interval);
    // A second pass has nothing to do for the script filter.
    run_on_own_context(service.extend());
    CHECK(service.checkpoints(script_type) == records);

    // Below the spending block everything is answered.
    auto const low = service.checkpoint({script_type, block_hash_at(sc, 100)});
    REQUIRE(low);
    CHECK(low->filter_headers.size() == records.size());
    auto const before = run_on_own_context(service.headers({script_type, 97, block_hash_at(sc, 100)}));
    REQUIRE(before);
    CHECK(before->previous_filter_header == script_filter_header_at(sc, 96));

    // At it and above it nothing is: no filter of the spending block, so no
    // filter header from there on.
    auto const spending = run_on_own_context(service.filter_of(block_hash_at(sc, spend_height), script_type));
    REQUIRE( ! spending);
    CHECK(spending.error() == error::not_found);
    CHECK( ! service.checkpoint({script_type, block_hash_at(sc, trunk_len)}));
    CHECK( ! run_on_own_context(service.headers({script_type, 100, block_hash_at(sc, spend_height)})));
    CHECK( ! run_on_own_context(service.headers({script_type, anchor_height + 1,
        block_hash_at(sc, anchor_height + 2)})));
    CHECK( ! run_on_own_context(service.range(range_request(block_hash_at(sc, spend_height), null_hash, 0))));
    CHECK( ! run_on_own_context(service.proof({script_type, block_hash_at(sc, spend_height), 0})));

    // A block above the anchor was validated and has its filter, and a wallet
    // that already trusts a filter header can still check it.
    auto const above = run_on_own_context(service.filter_of(block_hash_at(sc, anchor_height + 1), script_type));
    REQUIRE(above);
    CHECK(**above == served_script_filter(sc, anchor_height + 1));

    // The regular filter is served for every block all the same.
    CHECK(service.checkpoint({regular_type, block_hash_at(sc, trunk_len)}));
    CHECK(run_on_own_context(service.headers({regular_type, 100, block_hash_at(sc, spend_height)})));
}

#if ! defined(KTH_UTXOZ_REFERENCE_MODE)
TEST_CASE("getblock fails when the undo record does not cover what the block's transactions pay",
          "[node][full_validation][optimized][spent_outputs]") {
    auto const sc = build_scenario(/*tampered*/ false, 0);

    chain_fixture fixture("fv_spent_outputs_short", optimized_settings(sc));
    connect_on(fixture, sc);
    auto& chain = fixture.chain();

    // The spending block was connected without a record. Give it one that
    // reads well and holds the output it spends with one satoshi in it: less
    // than the transaction pays.
    auto const& spending = sc.blocks[spend_height - 1];
    auto const& funding = sc.blocks.front().transactions().front();
    auto short_output = funding.outputs()[0];
    short_output.set_value(1);
    auto const stored = database::utxo_entry{short_output, 1, 0, true}.to_data();

    database::block_undo undo;
    undo.spent.push_back(database::spent_output{
        utxoz::make_outpoint(std::span<uint8_t const, hash_size>{funding.hash()}, 0),
        std::vector<uint8_t>(stored.begin(), stored.end()), 1});
    auto const idx = chain.headers().find(spending.hash());
    REQUIRE(idx != blockchain::header_index::null_index);
    REQUIRE(chain.store_block_undo(idx, undo, spending.header().previous_block_hash()));

    // The record is read: the outputs come back, in the order of the inputs.
    auto const block = run_on_own_context(chain.fetch_block(spending.hash()));
    REQUIRE(block);
    auto const spent = run_on_own_context(chain.fetch_spent_outputs(spending.hash(), block->first));
    REQUIRE(spent);
    REQUIRE(spent->outputs.size() == 1);
    CHECK(spent->outputs.front().value() == 1);

    // It is not left out as a missing record is: the request fails.
    auto const decoded = get_block_reply(fixture, spending.hash(), 2);
    CHECK(decoded.find(R"("error":null)") == std::string::npos);
    CHECK(decoded.find("the undo record of the block does not give the fees of its transactions")
        != std::string::npos);
    CHECK(decoded.find(R"("fee":)") == std::string::npos);
    // The txids need no fees.
    CHECK(get_block_reply(fixture, spending.hash(), 1).find(R"("error":null)") != std::string::npos);
}
#endif
