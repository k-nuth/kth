// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/token_tests.cpp (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b), check_consensus_misc_activation
// and the with_mempool_check_* scenarios, against transaction::accept() with the
// prevouts supplied the way the validator supplies them.
// Original: Copyright (c) 2022-present The Bitcoin developers, MIT license.
//
// BCHN's CheckTxTokens maps to transaction::accept(); AreInputsStandard and
// IsStandardTx (policy) are classified separately in coverage.json.

#include <test_helpers.hpp>

#include <kth/domain/constants.hpp>

#include <limits>

using namespace kth;
namespace chain = kth::domain::chain;
namespace upgrade = kth::domain::machine::upgrade;

namespace {

constexpr size_t prevout_height = 100;


chain::output plain_output(uint64_t value) {
    short_hash h{};
    h.fill(0x11);
    return chain::output(value, chain::script(chain::script::to_pay_public_key_hash_pattern(h)), chain::token_data_opt{});
}

chain::output token_output(uint64_t value, chain::token_data_t td) {
    short_hash h{};
    h.fill(0x22);
    return chain::output(value, chain::script(chain::script::to_pay_public_key_hash_pattern(h)), chain::token_data_opt{std::move(td)});
}

// A spend of `prevout` (index `index` of `prevout_hash`) whose unlocking script is two
// 32-byte pushes, like BCHN's, so the transaction clears the 100-byte minimum.
chain::transaction spend(hash_digest const& prevout_hash, uint32_t index, chain::output const& prevout,
                         chain::output::list outputs) {
    data_chunk const push(32, 0x5a);
    kth::domain::machine::operation::list ops;
    ops.emplace_back(push, true);
    ops.emplace_back(push, true);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{prevout_hash, index}, chain::script(std::move(ops)), 0xffffffff);
    chain::transaction tx(1, 0, std::move(inputs), std::move(outputs));
    tx.inputs()[0].previous_output().validation.cache = prevout;
    tx.inputs()[0].previous_output().validation.height = prevout_height;
    return tx;
}

code accept(chain::transaction const& tx, kth::domain::script_flags_t flags) {
    return tx.accept(flags, prevout_height + 10, 1'700'000'000, 1'000'000, /*under_checkpoint*/ false,
                     /*transaction_pool*/ false, /*duplicate*/ false);
}

hash_digest hash_of(uint8_t seed) {
    hash_digest h{};
    h.fill(seed);
    return h;
}

auto const pre_activation = kth::domain::to_flags(upgrade::bch_gauss);        // before CashTokens
auto const descartes = kth::domain::to_flags(upgrade::bch_descartes);         // CashTokens, 40-byte commitments
auto const leibniz = kth::domain::to_flags(upgrade::bch_leibniz);             // 128-byte commitments

} // namespace

// check_consensus_misc_activation: a plain transaction passes before and after
TEST_CASE("BCHN check_consensus_misc_activation - regular transaction", "[bchn_non_vm][token][accept]") {
    auto const tx = spend(hash_of(1), 0, plain_output(100'000), {plain_output(90'000)});
    CHECK(accept(tx, pre_activation) == error::success);
    CHECK(accept(tx, descartes) == error::success);
}

// ... a token mint (genesis) tx: allowed to carry token outputs before activation
// (BCHN: "allow any txn vout scriptPubKey with prefix byte"), and valid after.
TEST_CASE("BCHN check_consensus_misc_activation - goodTokenMintTx", "[bchn_non_vm][token][accept]") {
    auto const prevout_hash = hash_of(2);
    auto const tx = spend(prevout_hash, 0, plain_output(100'000),
        {token_output(90'000, chain::make_both(prevout_hash, 0x42, chain::capability_t::minting, data_chunk(32, 0xcc)))});
    CHECK(accept(tx, pre_activation) == error::success);
    CHECK(accept(tx, descartes) == error::success);
}

// ... spending a token output before activation is rejected, after activation it is valid
TEST_CASE("BCHN check_consensus_misc_activation - goodTokenSpendTx", "[bchn_non_vm][token][accept]") {
    auto const id = hash_of(3);
    auto const held = token_output(100'000, chain::make_both(id, 0x42, chain::capability_t::minting, data_chunk(32, 0xcc)));
    auto const tx = spend(hash_of(4), 1, held, {token_output(90'000, chain::make_both(id, 0x42, chain::capability_t::minting, data_chunk(32, 0xcc)))});
    CHECK(accept(tx, pre_activation) == error::token_pre_activation_input);   // bad-txns-vin-tokenprefix-preactivation
    CHECK(accept(tx, descartes) == error::success);
}

TEST_CASE("BCHN check_consensus_misc_activation - goodTokenSpendEmptyCommitmentTx", "[bchn_non_vm][token][accept]") {
    auto const id = hash_of(5);
    auto const held = token_output(100'000, chain::make_both(id, 0x42, chain::capability_t::none, data_chunk{}));
    auto const tx = spend(hash_of(6), 1, held, {token_output(90'000, chain::make_both(id, 0x42, chain::capability_t::none, data_chunk{}))});
    CHECK(accept(tx, pre_activation) == error::token_pre_activation_input);
    CHECK(accept(tx, descartes) == error::success);
}

// ... a "mint" of a category that is not the spent outpoint: forbidden after activation
TEST_CASE("BCHN check_consensus_misc_activation - badTokenMintTx", "[bchn_non_vm][token][accept]") {
    auto const tx = spend(hash_of(7), 1 /* not output 0: cannot be a genesis */, plain_output(100'000),
        {token_output(90'000, chain::make_both(hash_of(7), 0x42, chain::capability_t::minting, data_chunk(32, 0xcc)))});
    CHECK(accept(tx, pre_activation) == error::success);
    CHECK(accept(tx, descartes) == error::token_invalid_category);   // bad-txns-token-invalid-category
}

// ... commitments over the limit: 40 bytes before Leibniz, 128 after
TEST_CASE("BCHN check_consensus_misc_activation - out of consensus commitment sizes", "[bchn_non_vm][token][accept]") {
    for (auto const [length, descartes_result, leibniz_result] : {
            std::tuple{size_t(40), error::success, error::success},
            std::tuple{size_t(41), error::token_commitment_oversized, error::success},
            std::tuple{size_t(128), error::token_commitment_oversized, error::success},
            std::tuple{size_t(129), error::token_commitment_oversized, error::token_commitment_oversized}}) {
        DYNAMIC_SECTION("commitment of " << length << " bytes") {
            auto const prevout_hash = hash_of(8);
            auto const tx = spend(prevout_hash, 0, plain_output(100'000),
                {token_output(90'000, chain::make_both(prevout_hash, 0x42, chain::capability_t::minting, data_chunk(length, 0xcc)))});
            CHECK(accept(tx, descartes) == descartes_result);
            CHECK(accept(tx, leibniz) == leibniz_result);
        }
    }
}

// with_mempool_check_invalid_mint
TEST_CASE("BCHN with_mempool_check_invalid_mint", "[bchn_non_vm][token][accept]") {
    auto const tx = spend(hash_of(9), 0, plain_output(100'000), {token_output(90'000, chain::make_fungible(hash_of(0x77), 1))});
    CHECK(accept(tx, descartes) == error::token_invalid_category);
}

// with_mempool_check_token_overspend
TEST_CASE("BCHN with_mempool_check_token_overspend", "[bchn_non_vm][token][accept]") {
    auto const id = hash_of(10);
    auto const held = token_output(100'000, chain::make_fungible(id, 1));
    CHECK(accept(spend(hash_of(11), 1, held, {token_output(90'000, chain::make_fungible(id, 2))}), descartes)
          == error::token_fungible_insufficient);   // bad-txns-token-in-belowout
    CHECK(accept(spend(hash_of(11), 1, held, {token_output(90'000, chain::make_fungible(id, 1))}), descartes)
          == error::success);
}

// with_mempool_check_invalid_nft_mint
TEST_CASE("BCHN with_mempool_check_invalid_nft_mint", "[bchn_non_vm][token][accept]") {
    auto const id = hash_of(12);
    auto const held = token_output(100'000, chain::make_both(id, 1, chain::capability_t::none, data_chunk(3, 0xaa)));
    CHECK(accept(spend(hash_of(13), 1, held, {token_output(90'000, chain::make_both(id, 1, chain::capability_t::none, data_chunk(3, 0xbb)))}), descartes)
          == error::token_nft_ex_nihilo);
    CHECK(accept(spend(hash_of(13), 1, held, {token_output(90'000, chain::make_both(id, 1, chain::capability_t::none, data_chunk(3, 0xaa)))}), descartes)
          == error::success);
}

// with_mempool_check_token_amount_overflow
TEST_CASE("BCHN with_mempool_check_token_amount_overflow", "[bchn_non_vm][token][accept]") {
    auto const prevout_hash = hash_of(14);
    auto const tx = spend(prevout_hash, 0, plain_output(100'000), {
        token_output(40'000, chain::make_fungible(prevout_hash, uint64_t(std::numeric_limits<int64_t>::max()))),
        token_output(40'000, chain::make_fungible(prevout_hash, 100))});
    CHECK(accept(tx, descartes) == error::token_amount_overflow);
}

// with_mempool_check_valid_genesis_token
TEST_CASE("BCHN with_mempool_check_valid_genesis_token", "[bchn_non_vm][token][accept]") {
    auto const prevout_hash = hash_of(15);
    auto const tx = spend(prevout_hash, 0, plain_output(100'000), {token_output(90'000, chain::make_fungible(prevout_hash, 1))});
    CHECK(accept(tx, descartes) == error::success);
}

// check_consensus_rejection_of_coinbase_tokens, post-activation half
TEST_CASE("BCHN check_consensus_rejection_of_coinbase_tokens - coinbase with a token output", "[bchn_non_vm][token][accept]") {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script(data_chunk(10, 0x51), false), 0xffffffff);
    chain::transaction const coinbase(1, 0, std::move(inputs), {token_output(50'000'000, chain::make_fungible(hash_of(16), 1))});
    REQUIRE(coinbase.is_coinbase());
    CHECK(accept(coinbase, descartes) == error::token_coinbase_has_tokens);
    CHECK(accept(coinbase, pre_activation) == error::success);
}

// Invalid-prefix JSON vectors that Knuth parses (as token data with is_valid() == false)
// instead of refusing: the transaction-level outcome is still a rejection, because
// validate_tokens refuses a fungible-only zero amount (vector 45) and an amount that is
// negative as int64 (vectors 53 and 54), which is what BCHN's CheckTxTokens outcome is
// (post-activation an unparseable prefix is "bad-txns-vout-tokenprefix"). Vector 46
// (NFT plus an explicit zero amount) is NOT rejected: failed_regression in coverage.json.
TEST_CASE("BCHN token_tests invalid prefix vectors 45, 53, 54 - rejected at transaction level", "[bchn_non_vm][token][accept]") {
    struct row { char const* name; char const* script_hex; code expected; };
    static row const rows[] = {
        {"vector 45: fungible-only amount 0", "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb1000", error::token_fungible_only_amount_zero},
        {"vector 53: amount 2^63 (fungible-only)", "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10ff0000000000000080", error::token_amount_negative},
        {"vector 54: amount 2^63 with NFT", "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb30ff0000000000000080", error::token_amount_negative},
    };
    for (auto const& r : rows) {
        DYNAMIC_SECTION(r.name) {
            auto script = decode_base16(r.script_hex);
            REQUIRE(script);
            data_chunk bytes(8, 0x00);
            bytes.push_back(uint8_t(script->size()));
            bytes.insert(bytes.end(), script->begin(), script->end());
            byte_reader reader(bytes);
            auto parsed = chain::output::from_data(reader, true);
            REQUIRE(parsed);
            REQUIRE(parsed->token_data().has_value());
            // spend an output of the same category so the category itself is not the failure
            auto const id = parsed->token_data()->id;
            auto const held = token_output(100'000, chain::make_both(id, 1, chain::capability_t::minting, data_chunk{}));
            chain::output out(90'000, parsed->script(), chain::token_data_opt{parsed->token_data()});
            auto const tx = spend(hash_of(40), 1, held, {out});
            CHECK(accept(tx, descartes) == r.expected);
        }
    }
}

// token_tests/check_tx_tokens_esoteric_failure_modes. BCHN corrupts the token data of a valid context
// (inputs and outputs) and expects a specific reject reason. Cases whose corruption cannot be expressed
// in Knuth's token_data_t (a bitfield that disagrees with the data: invalid bitfield, amount/commitment
// bitfield mismatch) are excluded in coverage.json; the others are below, each with the valid context
// as control.
namespace {

chain::transaction esoteric_context(bool has_nft, chain::token_data_t const* in0, chain::token_data_t const* out0, size_t in0_extra_commitment = 0) {
    (void)in0_extra_commitment;
    hash_digest const p0 = hash_of(0xa1);
    hash_digest const p1 = hash_of(0xa2);
    auto const make = [&](uint8_t cat) {
        return has_nft ? chain::make_both(hash_of(cat), 100, chain::capability_t::minting, data_chunk(32, 0xee))
                       : chain::make_fungible(hash_of(cat), 100);
    };
    auto const tok0 = make(0xb1);
    auto const tok1 = make(0xb2);
    data_chunk const push(32, 0x5a);
    kth::domain::machine::operation::list ops;
    ops.emplace_back(push, true);
    ops.emplace_back(push, true);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{p0, 0}, chain::script(ops), 0xffffffff);
    inputs.emplace_back(chain::output_point{p1, 0}, chain::script(ops), 0xffffffff);
    chain::output::list outputs;
    outputs.push_back(token_output(1'000'000, out0 ? *out0 : tok0));
    outputs.push_back(token_output(1'000'000, tok1));
    chain::transaction tx(1, 0, std::move(inputs), std::move(outputs));
    tx.inputs()[0].previous_output().validation.cache = token_output(1'000'000, in0 ? *in0 : tok0);
    tx.inputs()[1].previous_output().validation.cache = token_output(1'000'000, tok1);
    tx.inputs()[0].previous_output().validation.height = 100;
    tx.inputs()[1].previous_output().validation.height = 100;
    return tx;
}

} // namespace

TEST_CASE("BCHN check_tx_tokens_esoteric_failure_modes - valid context (control)", "[bchn_non_vm][token][accept]") {
    for (bool has_nft : {true, false}) {
        CHECK(accept(esoteric_context(has_nft, nullptr, nullptr), descartes) == error::success);
    }
}

TEST_CASE("BCHN check_tx_tokens_esoteric_failure_modes - negative amounts", "[bchn_non_vm][token][accept]") {
    // BCHN: bad-txns-token-amount-negative, in an input coin and in an output
    auto const bad = chain::make_both(hash_of(0xb1), uint64_t(-1), chain::capability_t::minting, data_chunk(32, 0xee));
    // an input coin carrying invalid token data is not a valid prefix output at all: output::is_valid() is false,
    // so transaction::accept reports the prevout as missing (rejected, but not with BCHN's reason)
    CHECK(accept(esoteric_context(true, &bad, nullptr), descartes) == error::missing_previous_output);
    CHECK(accept(esoteric_context(true, nullptr, &bad), descartes) == error::token_amount_negative);
}

TEST_CASE("BCHN check_tx_tokens_esoteric_failure_modes - spending zero fungible-only tokens", "[bchn_non_vm][token][accept]") {
    // BCHN: bad-txns-token-non-nft-amount-zero, input and output
    auto const zero = chain::make_fungible(hash_of(0xb1), 0);
    CHECK(accept(esoteric_context(false, &zero, nullptr), descartes) == error::missing_previous_output);   // as above for the input coin
    CHECK(accept(esoteric_context(false, nullptr, &zero), descartes) == error::token_fungible_only_amount_zero);
}

TEST_CASE("BCHN check_tx_tokens_esoteric_failure_modes - oversized commitment, input and output", "[bchn_non_vm][token][accept]") {
    // BCHN: bad-txns-token-commitment-oversized. upgrade 9: 40 bytes, upgrade 12: 128 bytes
    for (bool upgrade12 : {false, true}) {
        size_t const limit = upgrade12 ? 128 : 40;
        auto const flags = upgrade12 ? leibniz : descartes;
        auto const at_limit = chain::make_both(hash_of(0xb1), 100, chain::capability_t::minting, data_chunk(limit, 0xcc));
        auto const over = chain::make_both(hash_of(0xb1), 100, chain::capability_t::minting, data_chunk(limit + 1, 0xcc));
        DYNAMIC_SECTION((upgrade12 ? "upgrade 12 (128)" : "upgrade 9 (40)") << "") {
            CHECK(accept(esoteric_context(true, &at_limit, &at_limit), flags) == error::success);      // control: exactly the limit
            CHECK(accept(esoteric_context(true, &over, nullptr), flags) == error::token_commitment_oversized);
            CHECK(accept(esoteric_context(true, nullptr, &over), flags) == error::token_commitment_oversized);
        }
    }
}

TEST_CASE("BCHN check_tx_tokens_esoteric_failure_modes - inputs with token data before activation", "[bchn_non_vm][token][accept]") {
    // BCHN: bad-txns-vin-tokenprefix-preactivation; the second half (a locking script that merely starts
    // with the prefix byte) is the unparseable-prefix failed_regression
    CHECK(accept(esoteric_context(true, nullptr, nullptr), pre_activation) == error::token_pre_activation_input);
    CHECK(accept(esoteric_context(false, nullptr, nullptr), pre_activation) == error::token_pre_activation_input);
}
