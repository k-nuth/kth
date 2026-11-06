// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <future>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <kth/blockchain/populate/populate_base.hpp>

#include <kth/blockchain/pools/mempool.hpp>

#include "sync_harness.hpp"
#include "child_process_harness.hpp"
#if defined(KTH_CURRENCY_BCH)
#include "../../blockchain/test/transaction_size_fixture.hpp"
#include <kth/blockchain/validate/batch_validate.hpp>
#endif

using namespace kth;
using namespace kth::test;

// =============================================================================
// Validating a transaction, and admitting one
// =============================================================================
//
// These are two operations, and the difference between them used to be a flag:
// `organize` read `transaction_validation::simulate` and returned early before
// the mempool, so the caller that wanted validation alone set the flag on a
// store entry and called the admitting path anyway. What a call did was decided
// somewhere other than the call.
//
// `transaction_validate` is the explicit path and always was. What is pinned
// here is that the two do different things through their own names — nothing
// else says so once the flag is gone.

namespace {

// Run one of the chain's coroutines where the node runs them: on the chain's own
// priority pool, which its threads are already servicing.
//
// The wait is unbounded on purpose. These coroutines hold the chain and the
// transaction by reference and cannot be cancelled, so a deadline here could
// only abandon one — and an abandoned coroutine would still be running when the
// fixture tears the chain down underneath it. A test that hangs is diagnosable;
// one that returns while its work continues into freed memory is not. Bounding
// how long a test may run is the harness's job, and CI's.
template <typename Make>
code run_on_chain(blockchain::block_chain& chain, Make make) {
    auto future = ::asio::co_spawn(chain.executor(),
        [make = std::move(make)]() -> ::asio::awaitable<code> {
            co_return co_await make();
        },
        ::asio::use_future);

    return future.get();
}

// A chain of `trunk_len` blocks built through the node's own connect path, so
// the UTXO set a transaction is validated against was written the way the node
// writes it.
struct admission_chain {
    test::chain_fixture fixture;
    std::vector<domain::chain::block> trunk;

    explicit admission_chain(char const* tag, uint32_t trunk_len, bool require_standard = true, size_t policy_size = 1'000'000)
        : fixture(tag)
    {
        REQUIRE(fixture.created());
        fixture.settings().accept_non_standard_transactions = ! require_standard;
#if defined(KTH_CURRENCY_BCH)
        fixture.settings().standardness.maximum_transaction_size = policy_size;
#endif
        REQUIRE(fixture.start());

        auto const genesis = domain::chain::block::genesis_regtest();
        auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

        auto prev = genesis.hash();
        for (uint32_t h = 1; h <= trunk_len; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }

        auto const added = fixture.organizer().add_headers(headers_of(trunk));
        REQUIRE(added.headers_added == trunk_len);
        persist_headers(fixture, trunk, 1);
        connect_bodies(fixture, trunk, 1);

        // No restart. The connect path publishes the chain state at the close of
        // every batch (#605), so the state a transaction is validated against is
        // the one this chain just reached — which is what a running node needs,
        // since a node cannot restart itself to stay correct.
        auto const view = fixture.chain().chain_view();
        REQUIRE(view);
        REQUIRE(view->state->height() == trunk_len + 1u);
        REQUIRE(view->tip_hash == trunk.back().hash());
    }

    blockchain::block_chain& chain() { return fixture.chain(); }

    // A signed spend of the coinbase of block 1, which has matured by the tip of
    // a hundred-block trunk.
    [[nodiscard]]
    domain::chain::transaction matured_spend(uint64_t fee) {
        auto const& coinbase = trunk.front().transactions().front();
        return test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - fee,
                                 chain().chain_settings().enabled_flags());
    }
#if defined(KTH_CURRENCY_BCH)
    domain::chain::transaction p2sh_funding() {
        using namespace domain::chain;
        using domain::machine::operation;
        using domain::machine::opcode;
        // Re-sign after replacing the funding output.
        auto const redeem = data_chunk{0x51};
        auto const locking = script(script::to_pay_script_hash_pattern(bitcoin_short_hash(redeem)));
        auto const& cb = trunk.front().transactions().front();
        input::list ins{input{output_point{cb.hash(), 0}, script{}, 0xffffffff}};
        output::list outs{output{cb.outputs()[0].value() - 1000, locking, std::nullopt}};
        transaction unsigned_tx{1, 0, std::move(ins), std::move(outs)};
        unsigned_tx.inputs()[0].previous_output().validation.cache = cb.outputs()[0];
        auto sig = script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
            uint8_t(infrastructure::machine::sighash_algorithm::forkid_all),
            chain().chain_settings().enabled_flags(), cb.outputs()[0].value());
        REQUIRE(sig);
        input::list signed_ins{input{output_point{cb.hash(), 0},
            script(script::to_pay_public_key_hash_pattern_unlocking(*sig, miner_public())), 0xffffffff}};
        return transaction{1, 0, std::move(signed_ins), output::list(unsigned_tx.outputs())};
    }
#endif
};

constexpr uint32_t trunk_len = 100;
constexpr uint64_t fee = 1000;

} // namespace

TEST_CASE("validating a transaction does not admit it to the mempool", "[mempool][admission]") {
    admission_chain built("validate_only", trunk_len);
    auto& chain = built.chain();

    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    REQUIRE(chain.mempool_ref().size() == 0);

    auto const ec = run_on_chain(chain, [&chain, tx] { return chain.transaction_validate(tx); });

    // Validation ran and the transaction passed it — otherwise the pool being
    // empty below would prove nothing, since a rejected transaction is not
    // admitted either.
    CHECK(ec == error::success);
    CHECK(chain.mempool_ref().size() == 0);
    CHECK_FALSE(chain.mempool_ref().contains(tx->hash()));
}

TEST_CASE("organizing a valid transaction admits it to the mempool", "[mempool][admission]") {
    admission_chain built("organize_admits", trunk_len);
    auto& chain = built.chain();

    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    REQUIRE(chain.mempool_ref().size() == 0);

    auto const ec = run_on_chain(chain, [&chain, tx] { return chain.organize(tx); });

    CHECK(ec == error::success);
    CHECK(chain.mempool_ref().size() == 1);
    CHECK(chain.mempool_ref().contains(tx->hash()));
}

#if defined(KTH_CURRENCY_BCH)
namespace {

// What admission must answer for a serialized size: the consensus cap is
// unconditional. The default Knuth policy adds no size limit below it.
code expected_admission(uint32_t size) {
    if (size > 1000000) return error::transaction_size_limit;
    return error::success;
}

// True when the transaction was published to relay subscribers. The channel is
// the node's own announcement path; try_receive only observes what organize has
// already published, it does not wait.
bool announced(blockchain::block_chain::transaction_channel_ptr const& channel,
               hash_digest const& hash) {
    bool seen = false;
    while (channel->try_receive([&](std::error_code, transaction_const_ptr tx) {
        seen = seen || tx->hash() == hash;
    })) {}
    return seen;
}

template <typename Make>
size_t run_count_on_chain(blockchain::block_chain& chain, Make make) {
    return ::asio::co_spawn(chain.executor(),
        [make = std::move(make)]() -> ::asio::awaitable<size_t> { co_return co_await make(); },
        ::asio::use_future).get();
}

} // namespace

// Funding is P2SH OP_TRUE. No signature length or script resource limit can
// mask the serialized-size boundary, and every spendable output is P2PKH.
TEST_CASE("transaction size policy through real mempool admission", "[tx_size][admission]") {
    auto const require_standard = GENERATE(true, false);
    CAPTURE(require_standard);
    admission_chain built("size_admission", trunk_len, require_standard);
    auto& chain = built.chain();
    using namespace domain::chain;
    auto const funding = built.p2sh_funding();
    auto const funding_ptr = std::make_shared<domain::message::transaction>(funding);
    REQUIRE(run_on_chain(chain, [&] { return chain.organize(funding_ptr); }) == error::success);

    auto const channel = chain.subscribe_transaction();
    REQUIRE(channel);

    for (auto size : {99999u, 100000u, 100001u, 999999u, 1000000u, 1000001u}) {
        auto tx = sized_transaction(size, false, output_point{funding.hash(), 0});
        REQUIRE(tx.serialized_size(true) == size);
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        CAPTURE(size);
        auto const expected = expected_admission(size);

        // Validation only: same verdict, nothing admitted.
        CHECK(run_on_chain(chain, [&] { return chain.transaction_validate(ptr); }) == expected);
        CHECK_FALSE(chain.mempool_ref().contains(ptr->hash()));
        CHECK_FALSE(announced(channel, ptr->hash()));
    }

    // Refused sizes through the entry incoming relay uses: not pooled, not announced.
    for (auto size : {1000001u}) {
        auto tx = sized_transaction(size, false, output_point{funding.hash(), 0});
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        CAPTURE(size);
        auto const expected = expected_admission(size);
        CHECK(run_on_chain(chain, [&] { return chain.organize(ptr); }) == expected);
        CHECK_FALSE(chain.mempool_ref().contains(ptr->hash()));
        CHECK_FALSE(announced(channel, ptr->hash()));
    }

    // Accepted: admitted and announced.
    auto const accepted_size = 1000000u;
    auto tx = sized_transaction(accepted_size, false, output_point{funding.hash(), 0});
    auto ptr = std::make_shared<domain::message::transaction>(tx);
    CAPTURE(accepted_size);
    CHECK(run_on_chain(chain, [&] { return chain.organize(ptr); }) == error::success);
    CHECK(chain.mempool_ref().contains(ptr->hash()));
    CHECK(announced(channel, ptr->hash()));
    chain.unsubscribe_transaction(channel);
}

// A persisted mempool is re-admitted through the same checks, so a file written
// under a looser policy (or a different node) cannot smuggle in a transaction
// the current rules refuse.
TEST_CASE("mempool.dat revalidation applies the transaction size rules", "[tx_size][admission]") {
    auto const require_standard = GENERATE(true, false);
    auto const size = GENERATE(100000u, 100001u, 1000001u);
    CAPTURE(require_standard, size);
    admission_chain built("size_persisted", trunk_len, require_standard);
    auto& chain = built.chain();
    using namespace domain::chain;
    auto const funding = built.p2sh_funding();
    auto const sized = sized_transaction(size, false, output_point{funding.hash(), 0});

    auto const now = uint32_t(zulu_time());
    std::vector<database::mempool_stored_tx> stored{
        {std::make_shared<domain::message::transaction>(funding), now},
        {std::make_shared<domain::message::transaction>(sized), now + 1}};
    REQUIRE(database::store_mempool(built.fixture.dir() / "mempool.dat", std::move(stored)));

    auto const admitted = run_count_on_chain(chain, [&] { return chain.load_mempool_from_disk(); });
    auto const sized_ok = expected_admission(size) == error::success;
    CHECK(admitted == (sized_ok ? 2u : 1u));
    CHECK(chain.mempool_ref().contains(funding.hash()));
    CHECK(chain.mempool_ref().contains(sized.hash()) == sized_ok);
}

TEST_CASE("fast block validation enforces the consensus transaction size cap", "[tx_size][blocks]") {
    chain_fixture fixture("size_fast");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    using namespace domain::chain;
    for (auto const size : {999999u, 1000000u, 1000001u}) {
        for (bool cb : {false, true}) {
            auto tx = sized_transaction(size, cb);
            auto normal_cb = coinbase_tx(1, 0, block::subsidy(1, false));
            block blk{header{}, cb ? transaction::list{tx} : transaction::list{normal_cb, tx}};
            blk = block{header{1, null_hash, blk.generate_merkle_root(), 0, 0, 0},
                        transaction::list(blk.transactions())};
            std::vector<std::shared_ptr<light_block const>> light{to_light(blk)};
            CAPTURE(size, cb);
            // validate_chunk reports success as a default (system category) code.
            auto const result = run_on_chain(chain, [&] { return chain.validate_chunk(light, 1); });
            if (size > 1000000) {
                CHECK(result == error::transaction_size_limit);
            } else {
                CHECK_FALSE(result);
            }
        }
    }
}

TEST_CASE("checkpoint fast organization enforces the consensus transaction size cap", "[tx_size][blocks][organize_fast]") {
    auto const accept_nonstandard = GENERATE(false, true);
    chain_fixture fixture("size_organize_fast");
    REQUIRE(fixture.created());
    fixture.settings().accept_non_standard_transactions = accept_nonstandard;
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    using namespace domain::chain;
    for (auto const size : {999999u, 1000000u, 1000001u}) {
        for (bool cb : {false, true}) {
            auto tx = sized_transaction(size, cb);
            auto normal_cb = coinbase_tx(1, 0, block::subsidy(1, false));
            block candidate{header{}, cb ? transaction::list{tx} : transaction::list{normal_cb, tx}};
            candidate = block{header{1, null_hash, candidate.generate_merkle_root(), 0, 0, 0},
                              transaction::list(candidate.transactions())};
            auto const light = to_light(candidate);
            REQUIRE(light->is_valid_merkle_root());
            CAPTURE(size, cb);
            auto const expected = size > 1000000 ? code(error::transaction_size_limit) : code(error::success);
            CHECK(run_on_chain(chain, [&] { return chain.organize_fast(light, 1); }) == expected);
        }
    }
}

TEST_CASE("full block validation applies consensus size to otherwise valid transactions", "[tx_size][blocks]") {
    auto const size = GENERATE(999999u, 1000000u, 1000001u);
    auto const cb = GENERATE(false, true);
    admission_chain built("size_batch", trunk_len);
    auto& fixture = built.fixture;
    auto& chain = built.chain();
    auto const funding = built.p2sh_funding();
    auto funding_block = mine_block(built.trunk.back().hash(), 101,
        built.trunk.back().header().timestamp() + block_spacing, 0, {funding}, 1000);
    REQUIRE(fixture.organizer().add_headers({funding_block.header()}).headers_added == 1);
    persist_headers(fixture, {funding_block}, 101);
    connect_bodies(fixture, {funding_block}, 101);

    // A coinbase must carry the height-102 script to be otherwise valid.
    std::optional<domain::chain::script> cb_script;
    if (cb) cb_script = coinbase_script(102, 0);
    auto tx = sized_transaction(size, cb, domain::chain::output_point{funding.hash(), 0}, cb_script);
    auto const fees = cb ? 0 : funding.outputs()[0].value() - tx.total_output_value();
    auto blk = mine_block(funding_block.hash(), 102,
        funding_block.header().timestamp() + block_spacing, 0, cb ? std::vector<domain::chain::transaction>{}
                                                               : std::vector<domain::chain::transaction>{tx}, fees);
    if (cb) {
        // Re-mine with the large coinbase and its actual merkle root. Its value
        // is below the subsidy and its input script has a valid length.
        domain::chain::block candidate{blk.header(), {tx}};
        auto const merkle = candidate.generate_merkle_root();
        for (uint32_t nonce = 0; ; ++nonce) {
            domain::chain::header hdr{0x20000000, funding_block.hash(), merkle,
                blk.header().timestamp(), regtest_bits, nonce};
            if (hdr.is_valid_proof_of_work(domain::chain::hash(hdr), false)) {
                blk = domain::chain::block{hdr, {tx}};
                break;
            }
        }
    }
    REQUIRE(fixture.organizer().add_headers({blk.header()}).headers_added == 1);
    std::vector<block_const_ptr> blocks{std::make_shared<domain::message::block>(blk)};
    CAPTURE(size, cb);
    auto const expected = size > 1000000 ? code(error::transaction_size_limit) : code(error::success);
    CHECK(blockchain::validate_block_batch(chain, chain.chain_settings(),
        domain::config::network::regtest, blocks, 102) == expected);
}

TEST_CASE("Knuth policy mode controls local limits without bypassing consensus", "[tx_size][admission][standardness]") {
    auto const require_standard = GENERATE(true, false);
    admission_chain built("knuth_policy_mode", trunk_len, require_standard, 100000);
    auto& chain = built.chain();
    auto const funding = built.p2sh_funding();
    auto funding_ptr = std::make_shared<domain::message::transaction>(funding);
    REQUIRE(run_on_chain(chain, [&] { return chain.organize(funding_ptr); }) == error::success);
    auto const tx = sized_transaction(100001, false, domain::chain::output_point{funding.hash(), 0});
    auto ptr = std::make_shared<domain::message::transaction>(tx);
    auto const channel = chain.subscribe_transaction();
    // Contextual consensus must win over an otherwise failing local policy.
    domain::chain::transaction bad_version{3, 0, domain::chain::input::list(tx.inputs()),
                                           domain::chain::output::list(tx.outputs())};
    auto bad = std::make_shared<domain::message::transaction>(bad_version);
    CHECK(run_on_chain(chain, [&] { return chain.organize(bad); }) == error::invalid_tx_version);
    CHECK_FALSE(chain.mempool_ref().contains(bad->hash()));
    CHECK_FALSE(announced(channel, bad->hash()));
    auto const expected = require_standard ? code(error::standardness_transaction_size) : code(error::success);
    CHECK(run_on_chain(chain, [&] { return chain.organize(ptr); }) == expected);
    CHECK(chain.mempool_ref().contains(ptr->hash()) == !require_standard);
    CHECK(announced(channel, ptr->hash()) == !require_standard);
    chain.unsubscribe_transaction(channel);
}

#endif // KTH_CURRENCY_BCH

// A separate executable process lets the existing watchdog report a deadlock
// without abandoning any threads or forking a process with live Asio workers.
TEST_CASE("admission child: a suspended validation leaves the caller responsive", "[.admission-child]") {
    bool const cancel_waiter = GENERATE(false, true);
    bool const validate_only_waiter = GENERATE(false, true);
    watchdog guard(std::chrono::seconds(30), "concurrent transaction admission");
    {
        test::chain_fixture fixture("admission_deadlock");
        std::printf("ADMISSION-DIR:%s\n", fixture.dir().string().c_str());
        std::fflush(stdout);
        fixture.settings().cores = 1;
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto& chain = fixture.chain();
        std::promise<void> worker_entered, release_worker;
        auto released = release_worker.get_future().share();
        ::asio::post(chain.executor(), [&] { worker_entered.set_value(); released.wait(); });
        worker_entered.get_future().get();
        auto make_tx = [](uint8_t tag) {
            hash_digest hash{}; hash[0] = tag;
            domain::chain::input::list inputs{domain::chain::input{
                domain::chain::output_point{hash, 0}, domain::chain::script{}, 0xffffffff}};
            domain::chain::output::list outputs{domain::chain::output{1000,
                domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern(short_hash{})), std::nullopt}};
            return std::make_shared<domain::message::transaction>(1, 0, std::move(inputs), std::move(outputs));
        };
        ::asio::io_context caller;
        auto first = ::asio::co_spawn(caller, chain.organize(make_tx(1)), ::asio::use_future);
        caller.poll_one();
        bool const suspended = first.wait_for(std::chrono::seconds(0)) == std::future_status::timeout;
        ::asio::cancellation_signal cancel;
        auto second = ::asio::co_spawn(caller,
            validate_only_waiter ? chain.transaction_validate(make_tx(2)) : chain.organize(make_tx(2)),
            ::asio::bind_cancellation_slot(cancel.slot(), ::asio::use_future));
        if (cancel_waiter) ::asio::post(caller, [&] { cancel.emit(::asio::cancellation_type::terminal); });
        auto third = ::asio::co_spawn(caller, chain.transaction_validate(make_tx(3)), ::asio::use_future);
        bool heartbeat = false;
        ::asio::post(caller, [&] { heartbeat = true; });
        // The second admission is queued before the first one's completion.
        release_worker.set_value();
        caller.restart();
        caller.run();
        CHECK(suspended);
        CHECK(heartbeat);
        CHECK(first.get() == error::missing_previous_output);
        auto const second_result = second.get();
        if (cancel_waiter) {
            CHECK(second_result == ::asio::error::operation_aborted);
            CHECK(second_result != error::service_stopped);
        } else {
            CHECK(second_result == error::missing_previous_output);
        }
        CHECK(third.get() == error::missing_previous_output);
    }
    announce_completion();
}

TEST_CASE("concurrent admission leaves a single caller executor responsive", "[mempool][admission][deadlock]") {
    auto const child = run_child("admission child: a suspended validation leaves the caller responsive");
    // Clean a fixture left behind by the intentionally killed RED control.
    size_t offset = 0;
    while ((offset = child.out.find("ADMISSION-DIR:", offset)) != std::string::npos) {
        offset += std::string_view("ADMISSION-DIR:").size();
        auto const end = child.out.find('\n', offset);
        std::filesystem::path const dir(child.out.substr(offset, end - offset));
        if (dir.parent_path() == std::filesystem::temp_directory_path() &&
            dir.filename().string().starts_with("kth_reorg_e2e_admission_deadlock_")) {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }
    require_clean_child(child);
}

TEST_CASE("a prevout newer than the validation height is not a storage fault", "[mempool][admission][snapshot]") {
    admission_chain built("newer_prevout", trunk_len);
    auto& chain = built.chain();
    auto const& parent = built.trunk.back().transactions().front();
    domain::chain::output_point point{parent.hash(), 0};
    auto const bounded = chain.get_utxo(point, trunk_len - 1);
    REQUIRE_FALSE(bounded);
    REQUIRE(bounded.error() == database::result_code::key_not_found);
    struct probe : blockchain::populate_base {
        probe(executor_type executor, size_t threads, blockchain::block_chain const& chain)
            : populate_base(std::move(executor), threads, chain) {}
        using blockchain::populate_base::populate_prevout;
    } populate(chain.executor(), 1, chain);
    CHECK(populate.populate_prevout(trunk_len - 1, point, true, 0) == error::success);
    CHECK_FALSE(point.validation.cache.is_valid());
    CHECK(populate.populate_prevout(trunk_len, point, true, 0) == error::success);
    CHECK(point.validation.cache.is_valid());
    // A real operational failure must still fail closed, not become absence.
    {
        auto window = chain.begin_utxo_write();
        REQUIRE(window);
        window->mark_mutating();
    }
    auto const failed_read = chain.get_utxo(point, trunk_len);
    REQUIRE_FALSE(failed_read);
    REQUIRE(failed_read.error() == database::result_code::recovery_required);
    CHECK(populate.populate_prevout(trunk_len, point, true, 0) == error::operation_failed);
}

TEST_CASE("admission refuses an obsolete chain publication", "[mempool][admission][snapshot]") {
    admission_chain built("commit_snapshot", trunk_len);
    auto& chain = built.chain();
    auto const old = chain.chain_view();
    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    REQUIRE(run_on_chain(chain, [&chain, tx] { return chain.transaction_validate(tx); }) == error::success);
    auto const next = test::mine_block(built.trunk.back().hash(), trunk_len + 1,
        built.trunk.back().header().timestamp() + block_spacing, 0, {}, 0);
    std::vector<domain::chain::block> blocks{next};
    REQUIRE(built.fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
    persist_headers(built.fixture, blocks, trunk_len + 1);
    connect_bodies(built.fixture, blocks, trunk_len + 1);
    auto const entry = blockchain::mempool_entry{tx, fee, uint32_t(tx->serialized_size(true)), 1, 1};
    CHECK(chain.commit_validated_transaction(entry, old) == error::stale_chain);
    CHECK_FALSE(chain.mempool_ref().contains(tx->hash()));
    auto const current = chain.chain_view();
    REQUIRE(chain.begin_transition());
    CHECK(chain.commit_validated_transaction(entry, current) == error::stale_chain);
    chain.end_transition();
    CHECK(chain.commit_validated_transaction(entry, current) == error::success);
    CHECK(chain.mempool_ref().contains(tx->hash()));
}

TEST_CASE("admission revalidates once when a block arrives during validation", "[mempool][admission][snapshot]") {
    bool const invalidate_retry = GENERATE(false, true);
    admission_chain built("commit_retry", trunk_len);
    auto& chain = built.chain();
    auto const tx = std::make_shared<domain::message::transaction>(built.matured_spend(fee));
    ::asio::io_context caller;
    auto result = ::asio::co_spawn(caller, chain.organize(tx), ::asio::use_future);

    auto advance_to_validation = [&](size_t height) {
        // Only this context resumes the admission coroutine. Stop as soon as
        // accept publishes its scratch state, before the commit can run.
        bool reached = false;
        for (size_t step = 0; step < 32 && !reached; ++step) {
            REQUIRE(caller.run_one() == 1);
            chain.transaction_validations().visit(tx->hash(), [&](auto const& tv) {
                reached = tv.state && tv.state->height() == height;
            });
        }
        REQUIRE(reached);
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    };
    auto connect_next = [&](uint32_t height) {
        auto const block = test::mine_block(built.trunk.back().hash(), height,
            built.trunk.back().header().timestamp() + block_spacing, 0, {}, 0);
        std::vector<domain::chain::block> blocks{block};
        REQUIRE(built.fixture.organizer().add_headers(headers_of(blocks)).headers_added == 1);
        persist_headers(built.fixture, blocks, height);
        connect_bodies(built.fixture, blocks, height);
        built.trunk.push_back(block);
    };

    advance_to_validation(trunk_len + 1);
    connect_next(trunk_len + 1);
    if (invalidate_retry) {
        advance_to_validation(trunk_len + 2);
        connect_next(trunk_len + 2);
    }
    caller.run();
    CHECK(result.get() == (invalidate_retry ? error::stale_chain : error::success));
    CHECK(chain.mempool_ref().contains(tx->hash()) == !invalidate_retry);
    CHECK_FALSE(chain.transaction_validations().visit(tx->hash(), [](auto const&) {}));
}
