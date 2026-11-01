// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/pools/mempool.hpp>

#include "sync_harness.hpp"
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

    // `configure` sets anything else in the chain's settings before it starts.
    explicit admission_chain(char const* tag, uint32_t trunk_len, bool require_standard = true, size_t policy_size = 1'000'000,
        std::function<void(blockchain::settings&)> const& configure = {})
        : fixture(tag)
    {
        REQUIRE(fixture.created());
        fixture.settings().accept_non_standard_transactions = ! require_standard;
#if defined(KTH_CURRENCY_BCH)
        fixture.settings().standardness.maximum_transaction_size = policy_size;
#endif
        if (configure) configure(fixture.settings());
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

    // A signed spend of the coinbase of block `height`, paying `fee`. Matured
    // for heights up to the trunk's length less 99.
    [[nodiscard]]
    domain::chain::transaction coinbase_spend(uint32_t height, uint64_t fee) {
        auto const& coinbase = trunk[height - 1].transactions().front();
        return test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - fee,
                                 chain().chain_settings().enabled_flags());
    }
#if defined(KTH_CURRENCY_BCH)
    // A signed spend of block 1's coinbase into three P2PKH outputs, paying
    // `fee` satoshis: a payment with change to two parties, as wallets make.
    domain::chain::transaction three_output_spend(uint64_t fee) {
        using namespace domain::chain;
        auto const& cb = trunk.front().transactions().front();
        auto const share = (cb.outputs()[0].value() - fee) / 3;
        auto const outputs = [&] {
            return output::list{
                output{share, miner_script(), std::nullopt},
                output{share, miner_script(), std::nullopt},
                output{cb.outputs()[0].value() - fee - 2 * share, miner_script(), std::nullopt}};
        };
        input::list ins{input{output_point{cb.hash(), 0}, script{}, 0xffffffff}};
        transaction unsigned_tx{1, 0, std::move(ins), outputs()};
        unsigned_tx.inputs()[0].previous_output().validation.cache = cb.outputs()[0];
        auto sig = script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
            uint8_t(infrastructure::machine::sighash_algorithm::forkid_all),
            chain().chain_settings().enabled_flags(), cb.outputs()[0].value());
        REQUIRE(sig);
        input::list signed_ins{input{output_point{cb.hash(), 0},
            script(script::to_pay_public_key_hash_pattern_unlocking(*sig, miner_public())), 0xffffffff}};
        return transaction{1, 0, std::move(signed_ins), outputs()};
    }

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

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("a payment paying one satoshi per byte is admitted at the default price", "[mempool][admission][fee]") {
    // A wallet pays by size. With one input and three outputs the payment
    // carries three signature operations, so any per-sigop price of note would
    // exceed what one satoshi per byte pays for its ~260 bytes.
    admission_chain built("one_sat_per_byte", trunk_len);
    auto& chain = built.chain();
    CHECK(chain.chain_settings().sigop_fee_satoshis == 0.0f);

    auto const size = built.three_output_spend(0).serialized_size(true);
    auto const tx = std::make_shared<domain::message::transaction>(built.three_output_spend(size));
    // The fee is the funding value less the outputs: one satoshi per byte of a
    // payment that is about as large as the one it was measured on.
    auto const funding = built.trunk.front().transactions().front().outputs()[0].value();
    REQUIRE(funding - tx->total_output_value() == size);
    REQUIRE(tx->serialized_size(true) > 250u);
    REQUIRE(tx->serialized_size(true) <= size + 2);

    CHECK(run_on_chain(chain, [&chain, tx] { return chain.organize(tx); }) == error::success);
    CHECK(chain.mempool_ref().contains(tx->hash()));
}

TEST_CASE("a payment paying less than the per-byte price is still refused", "[mempool][admission][fee]") {
    // Only the per-signature-operation price is gone. A payment paying a
    // twentieth of a satoshi per byte, under the default 0.1, is refused as
    // before.
    admission_chain built("under_byte_price", trunk_len);
    auto& chain = built.chain();
    REQUIRE(chain.chain_settings().byte_fee_satoshis == 0.1f);

    auto const size = built.three_output_spend(0).serialized_size(true);
    auto const tx = std::make_shared<domain::message::transaction>(built.three_output_spend(size / 20));

    CHECK(run_on_chain(chain, [&chain, tx] { return chain.organize(tx); }) == error::insufficient_fee);
    CHECK_FALSE(chain.mempool_ref().contains(tx->hash()));
}

TEST_CASE("a per-signature-operation price set by the operator still applies", "[mempool][admission][fee]") {
    // node.sigop_fee_satoshis still prices signature operations when it is set.
    // At the old default of 100, the payment one satoshi per byte pays for is
    // refused, and admitted once it also pays for its three signature
    // operations.
    admission_chain built("operator_sigop_price", trunk_len, true, 1'000'000,
        [](blockchain::settings& settings) { settings.sigop_fee_satoshis = 100.0f; });
    auto& chain = built.chain();
    REQUIRE(chain.chain_settings().sigop_fee_satoshis == 100.0f);

    auto const size = built.three_output_spend(0).serialized_size(true);
    auto const by_size = std::make_shared<domain::message::transaction>(built.three_output_spend(size));
    CHECK(run_on_chain(chain, [&chain, by_size] { return chain.organize(by_size); }) == error::insufficient_fee);
    CHECK_FALSE(chain.mempool_ref().contains(by_size->hash()));

    auto const priced = std::make_shared<domain::message::transaction>(built.three_output_spend(size + 300));
    CHECK(run_on_chain(chain, [&chain, priced] { return chain.organize(priced); }) == error::success);
    CHECK(chain.mempool_ref().contains(priced->hash()));
}
#endif // KTH_CURRENCY_BCH

// =============================================================================
// A full mempool, a floor that rises and decays, and expiry
// =============================================================================

namespace {

// A trunk long enough that the coinbases of blocks 1 to 11 have matured.
constexpr uint32_t spendable_trunk_len = 110;
constexpr uint64_t start_time = 1'700'000'000;
constexpr uint64_t hour = 60 * 60;

std::shared_ptr<domain::message::transaction> as_message(domain::chain::transaction const& tx) {
    return std::make_shared<domain::message::transaction>(tx);
}

code organize_on(blockchain::block_chain& chain, transaction_const_ptr tx) {
    return run_on_chain(chain, [&chain, tx] { return chain.organize(tx); });
}

} // namespace

TEST_CASE("a full mempool evicts the lowest fee per byte and raises its floor", "[mempool][admission][fee][limit]") {
    // Room for two of these one-input, one-output payments (about 191 bytes
    // each), not three.
    admission_chain built("mempool_full", spendable_trunk_len, true, 1'000'000,
        [](blockchain::settings& settings) { settings.mempool_max_bytes = 450; });
    auto& chain = built.chain();
    std::atomic<uint64_t> now{start_time};
    chain.set_policy_clock([&now] { return now.load(); });
    REQUIRE(chain.fee_filter() == 100);

    auto const two = as_message(built.coinbase_spend(1, 400));
    auto const three = as_message(built.coinbase_spend(2, 600));
    REQUIRE(organize_on(chain, two) == error::success);
    REQUIRE(organize_on(chain, three) == error::success);

    // Cheaper than all the pool holds: it is the one evicted, so it is refused,
    // and the two already there stay.
    auto const one = as_message(built.coinbase_spend(3, 200));
    CHECK(organize_on(chain, one) == error::insufficient_fee);
    CHECK_FALSE(chain.mempool_ref().contains(one->hash()));
    CHECK(chain.mempool_ref().contains(two->hash()));
    CHECK(chain.mempool_ref().contains(three->hash()));
    CHECK(chain.mempool_ref().bytes() <= 450u);
    CHECK(chain.fee_filter() > 1000);

    // Paying more than the cheapest held: that one goes, with nothing else.
    auto const five = as_message(built.coinbase_spend(4, 1000));
    CHECK(organize_on(chain, five) == error::success);
    CHECK_FALSE(chain.mempool_ref().contains(two->hash()));
    CHECK(chain.mempool_ref().contains(three->hash()));
    CHECK(chain.mempool_ref().contains(five->hash()));
    CHECK(chain.mempool_ref().bytes() <= 450u);

    // The floor is now the evicted rate (about 2.1 per byte) plus the
    // configured 0.1, so a payment of about 1.6 per byte is refused on price.
    auto const raised = chain.fee_filter();
    CHECK(raised > 2000);
    auto const under = as_message(built.coinbase_spend(5, 300));
    CHECK(organize_on(chain, under) == error::insufficient_fee);

    // The excess halves every 12 hours, and is gone within days.
    now += 12 * hour;
    CHECK(chain.fee_filter() < raised);
    CHECK(chain.fee_filter() > 100);
    now += 7 * 24 * hour;
    CHECK(chain.fee_filter() == 100);
}

TEST_CASE("an unconfirmed transaction expires with its descendants when a block connects", "[mempool][admission][expiry]") {
    admission_chain built("mempool_expiry", spendable_trunk_len);
    auto& chain = built.chain();
    REQUIRE(chain.chain_settings().mempool_expiry_hours == 336u);
    std::atomic<uint64_t> now{start_time};
    chain.set_policy_clock([&now] { return now.load(); });

    auto const parent = built.coinbase_spend(1, 1000);
    REQUIRE(organize_on(chain, as_message(parent)) == error::success);
    now += hour;
    auto const child = test::spend_p2pkh(parent, 0, parent.outputs()[0].value() - 1000,
        chain.chain_settings().enabled_flags());
    REQUIRE(organize_on(chain, as_message(child)) == error::success);
    now += hour;
    auto const other = built.coinbase_spend(2, 1000);
    REQUIRE(organize_on(chain, as_message(other)) == error::success);

    // Not yet: 335 hours after the parent was first seen.
    now = start_time + 335 * hour;
    chain.mempool_remove_for_block(built.trunk.back());
    CHECK(chain.mempool_ref().contains(parent.hash()));

    // 337 hours: the parent has expired, and its child (seen an hour later, so
    // not expired itself) goes with it; the one seen two hours later stays.
    now = start_time + 337 * hour;
    chain.mempool_remove_for_block(built.trunk.back());
    CHECK_FALSE(chain.mempool_ref().contains(parent.hash()));
    CHECK_FALSE(chain.mempool_ref().contains(child.hash()));
    CHECK(chain.mempool_ref().contains(other.hash()));
}

TEST_CASE("a transaction reloaded from mempool.dat keeps its age", "[mempool][admission][expiry]") {
    admission_chain built("mempool_reload_age", spendable_trunk_len);
    auto& chain = built.chain();
    std::atomic<uint64_t> now{start_time};
    chain.set_policy_clock([&now] { return now.load(); });

    auto const expired = built.coinbase_spend(1, 1000);
    auto const recent = built.coinbase_spend(2, 1000);
    std::vector<database::mempool_stored_tx> stored{
        {as_message(expired), start_time - 337 * hour},
        {as_message(recent), start_time - hour}};
    REQUIRE(database::store_mempool(built.fixture.dir() / "mempool.dat", std::move(stored)));

    auto const admitted = run_count_on_chain(chain, [&] { return chain.load_mempool_from_disk(); });
    CHECK(admitted == 1u);
    CHECK_FALSE(chain.mempool_ref().contains(expired.hash()));
    auto const entry = chain.mempool_ref().entry(recent.hash());
    REQUIRE(entry.has_value());
    CHECK(entry->time_seen == start_time - hour);
}

TEST_CASE("a mempool limit or expiry of zero turns it off", "[mempool][admission][limit][expiry]") {
    admission_chain built("mempool_unlimited", spendable_trunk_len, true, 1'000'000,
        [](blockchain::settings& settings) {
            settings.mempool_max_bytes = 0;
            settings.mempool_expiry_hours = 0;
        });
    auto& chain = built.chain();
    std::atomic<uint64_t> now{start_time};
    chain.set_policy_clock([&now] { return now.load(); });

    auto const first = as_message(built.coinbase_spend(1, 200));
    auto const second = as_message(built.coinbase_spend(2, 200));
    REQUIRE(organize_on(chain, first) == error::success);
    REQUIRE(organize_on(chain, second) == error::success);
    CHECK(chain.fee_filter() == 100);

    now += 10'000 * hour;
    chain.mempool_remove_for_block(built.trunk.back());
    CHECK(chain.mempool_ref().contains(first->hash()));
    CHECK(chain.mempool_ref().contains(second->hash()));
}
