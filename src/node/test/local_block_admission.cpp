// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/node.hpp>
#if defined(KTH_WITH_RPC)
#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/json.hpp>
#endif

#include "../../blockchain/test/regtest_miner.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// Local block admission, through the node
// =============================================================================
//
// A block handed to block_chain::submit_block on a running node goes through
// the same pipeline a peer's block does — the organizer judges the header, fast
// validation and storage take the body, the builder judges it in context and
// connects it — and the submitter is told what came of it, once.
//
// Every case runs a real full_node on regtest with no peers: nothing here
// arrives from the network, so whatever the chain does, submit_block did.
//
// On a node without this bridge a submission is never answered: the case that
// expects an answer waits its limit out and fails for that reason.

namespace {

constexpr uint32_t spacing = 120;
constexpr auto answer_limit = std::chrono::seconds(60);
constexpr auto stop_limit = std::chrono::seconds(120);

struct owned_directory {
    std::filesystem::path directory;
    bool owned;
    bool keep{false};

    explicit owned_directory(std::string const& tag, bool reopen = false)
        : directory(std::filesystem::temp_directory_path() /
            ("kth_local_admission_" + tag + "_" + std::to_string(getpid())))
        , owned(false) {
        std::error_code ec;
        if (reopen) {
            owned = std::filesystem::is_directory(directory, ec);
        } else {
            std::filesystem::remove_all(directory, ec);
            owned = std::filesystem::create_directories(directory, ec);
        }
    }

    ~owned_directory() {
        if (owned && ! keep) {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
        }
    }

    owned_directory(owned_directory const&) = delete;
    owned_directory& operator=(owned_directory const&) = delete;
};

node::configuration regtest_configuration(std::filesystem::path const& directory, std::string const& tag) {
    node::configuration config(domain::config::network::regtest);
    config.database.directory = directory;
    config.network.inbound_port = 0;
    config.network.inbound_connections = 0;
    config.network.outbound_connections = 0;       // nothing dials out
    config.network.threads = 1;
    config.network.hosts_file = directory / (tag + "_hosts.dat");
    config.network.banlist_file = directory / (tag + "_banlist.dat");
    config.network.peers_file = directory / (tag + "_peers.dat");
    config.rpc.enabled = false;
    return config;
}

// A node started and running, stopped and joined in the order the node's own
// owner would, and asked things the way an embedder asks them.
struct running_node {
    owned_directory storage;
    node::configuration config;        // the node keeps references into it
    node::full_node node;
    std::future<code> run;
    bool stopped{false};

    explicit running_node(std::string const& tag, bool reopen = false)
        : storage(tag, reopen)
        , config(regtest_configuration(storage.directory, tag))
        , node(config) {
        REQUIRE(storage.owned);
        if ( ! reopen) {
            database::data_base db(config.database);
            REQUIRE(db.create(node::full_node::get_genesis_block(config.net)));
        }
        auto started = ::asio::co_spawn(node.network_thread_pool().get_executor(),
            node.start(), ::asio::use_future);
        REQUIRE(started.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
        REQUIRE(started.get() == error::success);

        run = ::asio::co_spawn(node.network_thread_pool().get_executor(),
            node.run(), ::asio::use_future);
    }

    ~running_node() {
        if ( ! stopped) {
            (void)stop();
        }
    }

    running_node(running_node const&) = delete;
    running_node& operator=(running_node const&) = delete;

    // What submit_block answered, or nothing if it did not answer in time.
    // Driven on a context of the caller's own, as an embedder would: the
    // answer is read through that context, not through the node's pool.
    std::optional<code> submit(domain::chain::block const& blk,
                               std::chrono::milliseconds limit = answer_limit) {
        ::asio::io_context own;
        auto result = ::asio::co_spawn(own,
            node.chain().submit_block(std::make_shared<domain::message::block const>(blk)),
            ::asio::use_future);
        own.run_for(limit);
        if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return std::nullopt;
        }
        return result.get().error;
    }

    // Answers whether run() finished within the limit.
    bool stop() {
        stopped = true;
        node.stop();
        bool const finished = run.wait_for(stop_limit) == std::future_status::ready;
        if (finished) {
            (void)run.get();
        }
        node.join();
        return finished;
    }

    blockchain::block_chain& chain() { return node.chain(); }

    hash_digest connected_tip() {
        auto const view = chain().chain_view();
        REQUIRE(view);
        return view->tip_hash;
    }

    size_t connected_height() {
        auto const view = chain().chain_view();
        REQUIRE(view);
        return view->connected_tip_height;
    }

    int32_t header_tip_height() {
        return chain().headers().active_tip_height();
    }

    uint32_t built_height() {
        auto const built = chain().get_utxo_built_height();
        REQUIRE(built);
        return *built;
    }
};

// Blocks that end near the present, so the connect path treats the chain as
// current rather than as an initial sync that batches a thousand at a time.
uint32_t timestamp_at(uint32_t height) {
    return uint32_t(zulu_time()) - 30 * spacing + height * spacing;
}

domain::chain::block valid_block(hash_digest const& prev, uint32_t height, uint32_t salt = 0) {
    return mine_block(prev, height, timestamp_at(height), salt, {}, 0);
}

domain::chain::block mine_with(hash_digest const& prev, uint32_t timestamp,
                               domain::chain::transaction::list txs) {
    domain::chain::block candidate(
        domain::chain::header{0x20000000, prev, null_hash, timestamp, regtest_bits, 0}, std::move(txs));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{0x20000000, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return domain::chain::block(std::move(header),
                domain::chain::transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

// A block whose coinbase claims one satoshi more than it may: a valid header
// over a body that fails only in context.
domain::chain::block overclaiming_block(hash_digest const& prev, uint32_t height) {
    domain::chain::transaction::list txs;
    txs.push_back(coinbase_tx(height, 0, domain::chain::block::subsidy(height, false) + 1));
    return mine_with(prev, timestamp_at(height), std::move(txs));
}

// `valid`'s header over a different body: the merkle root does not match.
domain::chain::block wrong_body_for(domain::chain::block const& valid, uint32_t height) {
    domain::chain::transaction::list txs;
    txs.push_back(coinbase_tx(height, 77, domain::chain::block::subsidy(height, false)));
    return domain::chain::block(valid.header(), std::move(txs));
}

// Submit the next blocks of a chain, each on the one before, requiring each to
// be connected before the next is offered. Returns the blocks.
std::vector<domain::chain::block> connect_chain(running_node& run, uint32_t count) {
    std::vector<domain::chain::block> blocks;
    auto prev = domain::chain::block::genesis_regtest().hash();
    for (uint32_t h = 1; h <= count; ++h) {
        blocks.push_back(valid_block(prev, h));
        auto const answered = run.submit(blocks.back());
        REQUIRE(answered.has_value());
        REQUIRE(*answered == error::success);
        prev = blocks.back().hash();
    }
    return blocks;
}

} // namespace

// -----------------------------------------------------------------------------
// The one answer, and what it stands for
// -----------------------------------------------------------------------------

TEST_CASE("local admission: a block on the connected tip is connected and the submitter is told so",
          "[local_admission][integration]") {
    running_node run("connect");

    auto const chain = connect_chain(run, 3);

    // Connected means CONNECTED: the published view names it and the UTXO set
    // reaches it, not merely that it was taken in.
    CHECK(run.connected_tip() == chain.back().hash());
    CHECK(run.connected_height() == 3u);
    CHECK(run.built_height() == 3u);
    CHECK(run.header_tip_height() == 3);

    // The same block again is a duplicate, answered at once.
    auto const again = run.submit(chain.back());
    REQUIRE(again.has_value());
    CHECK(*again == error::duplicate_block);
    CHECK(run.connected_height() == 3u);

    CHECK(run.stop());
}

TEST_CASE("local admission: a block without its parent, and one on a side branch, are answered without being connected",
          "[local_admission][integration]") {
    running_node run("orphan_and_side");

    auto const chain = connect_chain(run, 3);

    // Builds on a block nothing has heard of.
    hash_digest nowhere{};
    nowhere.fill(0xab);
    auto const orphan = run.submit(valid_block(nowhere, 4));
    REQUIRE(orphan.has_value());
    CHECK(*orphan == error::orphan_block);

    // Builds on block 2: a sibling of the tip, valid as a header, not heavier.
    auto const side = run.submit(valid_block(chain[1].hash(), 3, /*salt*/ 9));
    REQUIRE(side.has_value());
    CHECK(*side == error::insufficient_work);

    CHECK(run.connected_tip() == chain.back().hash());
    CHECK(run.connected_height() == 3u);
    CHECK(run.header_tip_height() == 3);

    CHECK(run.stop());
}

// -----------------------------------------------------------------------------
// Bodies the pipeline refuses
// -----------------------------------------------------------------------------

TEST_CASE("local admission: a body that does not match its header is refused and the header is kept for the right one",
          "[local_admission][integration][body]") {
    running_node run("wrong_body");

    auto const chain = connect_chain(run, 3);
    auto const right = valid_block(chain.back().hash(), 4);
    auto const wrong = wrong_body_for(right, 4);
    REQUIRE(wrong.hash() == right.hash());
    REQUIRE_FALSE(wrong.is_valid_merkle_root());

    // The header is valid and is taken; the body is not that header's.
    auto const refused = run.submit(wrong);
    REQUIRE(refused.has_value());
    CHECK(*refused == error::merkle_mismatch);
    CHECK(run.header_tip_height() == 4);
    CHECK(run.connected_height() == 3u);

    // A wrong body says nothing about the header: the right one connects.
    auto const accepted = run.submit(right);
    REQUIRE(accepted.has_value());
    CHECK(*accepted == error::success);
    CHECK(run.connected_tip() == right.hash());
    CHECK(run.connected_height() == 4u);
    CHECK(run.built_height() == 4u);

    CHECK(run.stop());
}

TEST_CASE("local admission: a body the builder refuses in context is answered with the reason, and the node goes on",
          "[local_admission][integration][contextual]") {
    running_node run("contextual_reject");

    auto const chain = connect_chain(run, 3);
    auto const bad = overclaiming_block(chain.back().hash(), 4);
    REQUIRE(bad.is_valid_merkle_root());

    // Header valid, body stored, builder says no: the verdict comes from the
    // one validator that has the context, and names the rule.
    auto const refused = run.submit(bad);
    REQUIRE(refused.has_value());
    CHECK(*refused == error::coinbase_value_limit);

    // The chain stepped off it: nothing connected, the header tip back on the
    // block below, the persisted table agreeing.
    CHECK(run.connected_tip() == chain.back().hash());
    CHECK(run.connected_height() == 3u);
    CHECK(run.built_height() == 3u);
    CHECK(run.header_tip_height() == 3);
    {
        auto const persisted = run.chain().get_header(3);
        REQUIRE(persisted);
        CHECK(domain::chain::hash(*persisted) == chain.back().hash());
        CHECK_FALSE(run.chain().get_header(4));
    }

    // Offering the same body again is not judged again.
    auto const again = run.submit(bad);
    REQUIRE(again.has_value());
    CHECK(*again == error::duplicate_block);

    // Neither the builder nor the coordinator died of it: a valid block at the
    // same height connects.
    auto const good = valid_block(chain.back().hash(), 4, /*salt*/ 1);
    auto const accepted = run.submit(good);
    REQUIRE(accepted.has_value());
    CHECK(*accepted == error::success);
    CHECK(run.connected_tip() == good.hash());
    CHECK(run.connected_height() == 4u);
    CHECK(run.built_height() == 4u);
    CHECK(run.header_tip_height() == 4);

    CHECK(run.stop());
}

// -----------------------------------------------------------------------------
// Shutdown
// -----------------------------------------------------------------------------

TEST_CASE("local admission: a submission the node cannot finish is answered at shutdown, and none is taken after",
          "[local_admission][integration][shutdown]") {
    running_node run("shutdown");

    auto const chain = connect_chain(run, 2);

    // Header 3 is taken without a body (a wrong body is refused), then block 4
    // on top of it: its header is active and its body is stored, but nothing
    // will ever bring block 3's body on a node with no peers, so it cannot be
    // connected and the submission stays open.
    auto const three = valid_block(chain.back().hash(), 3);
    auto const refused = run.submit(wrong_body_for(three, 3));
    REQUIRE(refused.has_value());
    REQUIRE(*refused == error::merkle_mismatch);
    REQUIRE(run.header_tip_height() == 3);

    auto const four = valid_block(three.hash(), 4);
    auto pending = ::asio::co_spawn(run.node.network_thread_pool().get_executor(),
        run.chain().submit_block(std::make_shared<domain::message::block const>(four)),
        ::asio::use_future);
    REQUIRE(pending.wait_for(std::chrono::seconds(3)) == std::future_status::timeout);
    REQUIRE(run.header_tip_height() == 4);
    REQUIRE(run.connected_height() == 2u);
    auto repeated = ::asio::co_spawn(run.node.network_thread_pool().get_executor(),
        run.chain().submit_block(std::make_shared<domain::message::block const>(four)),
        ::asio::use_future);
    REQUIRE(repeated.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    auto const duplicate = repeated.get();
    CHECK(duplicate.error == error::duplicate_block);
    CHECK(duplicate.duplicate);
    CHECK(duplicate.validity == blockchain::block_validity::unknown);
    CHECK(pending.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);

    // Stopping the node is what answers it. The answer is read while the
    // node's pool is still running — a stop, not yet a join — since that is
    // where the waiter was spawned.
    auto const started = std::chrono::steady_clock::now();
    run.node.stop();
    REQUIRE(pending.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    CHECK(pending.get().error == error::service_stopped);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(30));
    REQUIRE(run.stop());

    // And nothing is taken from here on: refused at once, not queued. Driven
    // on a context of the test's own, since the node's pool is joined.
    ::asio::io_context own;
    auto after = ::asio::co_spawn(own,
        run.chain().submit_block(std::make_shared<domain::message::block const>(
            valid_block(four.hash(), 5))),
        ::asio::use_future);
    own.run();
    REQUIRE(after.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK(after.get().error == error::service_stopped);
}

TEST_CASE("local admission: rejecting the first block preserves genesis and admits a replacement",
          "[local_admission][integration][genesis_rejection]") {
    running_node run("genesis_rejection");
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const refused = run.submit(overclaiming_block(genesis.hash(), 1));
    REQUIRE(refused.has_value());
    CHECK(*refused == error::coinbase_value_limit);
    REQUIRE_FALSE(run.node.stopped());
    CHECK(run.header_tip_height() == 0);
    CHECK(run.connected_tip() == genesis.hash());
    auto const stored_genesis = run.chain().get_header(0);
    REQUIRE(stored_genesis);
    CHECK(domain::chain::hash(*stored_genesis) == genesis.hash());
    CHECK_FALSE(run.chain().get_header(1));

    // This must be the same live owner, not a restart hiding a failed node.
    auto const good = valid_block(genesis.hash(), 1, 1);
    auto const accepted = run.submit(good);
    REQUIRE(accepted.has_value());
    CHECK(*accepted == error::success);
    CHECK(run.connected_tip() == good.hash());
    CHECK(run.built_height() == 1u);
    CHECK(run.stop());
}

TEST_CASE("local admission: the owner distinguishes connected and invalid duplicates",
          "[local_admission][integration][duplicate]") {
    running_node run("duplicate_outcome");
    auto detailed = [&](domain::chain::block const& block) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io,
            run.chain().submit_block(std::make_shared<domain::message::block const>(block)),
            ::asio::use_future);
        io.run_for(answer_limit);
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    };
    auto const block = valid_block(run.connected_tip(), 1);
    auto const accepted = detailed(block);
    CHECK(accepted.error == error::success);
    CHECK(accepted.validity == blockchain::block_validity::valid);
    CHECK_FALSE(accepted.duplicate);
    REQUIRE(run.connected_tip() == block.hash());
    auto const duplicate = detailed(block);
    CHECK(duplicate.error == error::duplicate_block);
    CHECK(duplicate.validity == blockchain::block_validity::valid);
    CHECK(duplicate.duplicate);

    auto const invalid = overclaiming_block(block.hash(), 2);
    auto const rejected = detailed(invalid);
    CHECK(rejected.error == error::coinbase_value_limit);
    CHECK(rejected.validity == blockchain::block_validity::invalid);
    CHECK_FALSE(rejected.duplicate);
    auto const repeated = detailed(invalid);
    CHECK(repeated.error == error::duplicate_block);
    CHECK(repeated.validity == blockchain::block_validity::invalid);
    CHECK(repeated.duplicate);
    CHECK(run.connected_tip() == block.hash());
    // Callers that only need the error select it from the same reply.
    auto const legacy = run.submit(invalid);
    REQUIRE(legacy.has_value());
    CHECK(*legacy == error::duplicate_block);
    REQUIRE(run.stop());
}

TEST_CASE("local admission: rejected block identity survives a node restart",
          "[local_admission][integration][restart]") {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const one = valid_block(genesis.hash(), 1);
    auto const invalid = overclaiming_block(one.hash(), 2);
    {
        running_node run("rejected_restart");
        REQUIRE(run.submit(one) == code(error::success));
        REQUIRE(run.submit(invalid) == code(error::coinbase_value_limit));
        REQUIRE(run.stop());
        run.storage.keep = true;
    }
    {
        running_node run("rejected_restart", true);
        REQUIRE(run.connected_tip() == one.hash());
        ::asio::io_context io;
        auto pending = ::asio::co_spawn(io,
            run.chain().submit_block(std::make_shared<domain::message::block const>(invalid)),
            ::asio::use_future);
        io.run_for(answer_limit);
        REQUIRE(pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        auto const duplicate = pending.get();
        CHECK(duplicate.error == error::duplicate_block);
        CHECK(duplicate.validity == blockchain::block_validity::invalid);
        CHECK(duplicate.duplicate);
        CHECK(run.connected_tip() == one.hash());
        auto const replacement = valid_block(one.hash(), 2, 19);
        REQUIRE(run.submit(replacement) == code(error::success));
        CHECK(run.connected_tip() == replacement.hash());
        REQUIRE(run.stop());
    }
}

TEST_CASE("local admission: interrupted rejection recovery persists replacement headers",
          "[local_admission][integration][rejection_recovery]") {
    std::string const tag = "interrupted_rejection_restart";
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const one = valid_block(genesis.hash(), 1);
    auto const invalid = overclaiming_block(one.hash(), 2);
    auto const child = valid_block(invalid.hash(), 3);
    auto const replacement = valid_block(one.hash(), 2, 19);
    std::filesystem::path directory;
    {
        running_node run(tag);
        REQUIRE(run.submit(one) == code(error::success));
        REQUIRE(run.stop());
        directory = run.storage.directory;
        run.storage.keep = true;
    }
    {
        // Model the durable crash boundary: the invalid verdict committed but
        // its unconnected header suffix was not yet removed. No node is live.
        auto const config = regtest_configuration(directory, tag);
        database::data_base db(config.database);
        REQUIRE(db.open());
        REQUIRE_FALSE(db.push_headers_batch({invalid.header(), child.header()}, 2));
        REQUIRE(database::succeed(db.internal_db().record_rejected_block(invalid.hash())));
    }
    {
        running_node run(tag, true);
        CHECK(run.connected_tip() == one.hash());
        auto const heights = run.chain().get_last_heights();
        REQUIRE(heights);
        CHECK(heights->header == 1);
        REQUIRE(run.submit(replacement) == code(error::success));
        CHECK(run.connected_tip() == replacement.hash());
        REQUIRE(run.stop());
        run.storage.keep = true;
    }
    {
        // The coordinator's persistence cursor must have rewound as well:
        // acceptance alone can hide a replacement header never written to LMDB.
        running_node run(tag, true);
        CHECK(run.connected_tip() == replacement.hash());
        auto const header = run.chain().get_header(2);
        REQUIRE(header);
        CHECK(domain::chain::hash(*header) == replacement.hash());
        REQUIRE(run.stop());
    }
}

#if defined(KTH_WITH_RPC)
TEST_CASE("submitblock RPC publishes acceptance and returns standard duplicates", "[local admission][rpc submitblock]") {
    running_node run("rpc_submitblock");
    node::mining::job_store jobs(1, 60);
    node::rpc::method_context context{run.chain(), jobs};
    node::rpc::dispatcher dispatcher;
    node::rpc::register_builtin_methods(dispatcher);
    auto request = [&](domain::chain::block const& block, std::string const& suffix) {
        auto const body = std::string(R"({"method":"submitblock","params":[")")
            + encode_base16(to_data_chunk(block)) + '"' + suffix + R"(],"id":9})";
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, dispatcher.handle(context, body), ::asio::use_future);
        io.run_for(answer_limit);
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    };
    auto const block = valid_block(run.connected_tip(), 1);
    REQUIRE(request(block, "") == node::rpc::build_success("9", "null"));
    REQUIRE(run.connected_tip() == block.hash());
    for (auto const* suffix : {"", ",\"dummy\"", ",null", ",{}"}) {
        CAPTURE(suffix);
        CHECK(request(block, suffix) == node::rpc::build_success("9", R"("duplicate")"));
    }
    auto const invalid = mine_block(block.hash(), 2, timestamp_at(2), 17, {}, 1);
    CHECK(request(invalid, "") == node::rpc::build_success("9", R"("bad-cb-amount")"));
    CHECK(run.connected_tip() == block.hash());
    CHECK(request(invalid, "") == node::rpc::build_success("9", R"("duplicate-invalid")"));
    REQUIRE(run.stop());
}

#endif
