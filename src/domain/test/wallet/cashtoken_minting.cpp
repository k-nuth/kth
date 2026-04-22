// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <limits>
#include <string>

#include <kth/domain/wallet/cashtoken_minting.hpp>
#include <kth/domain/wallet/payment_address.hpp>

using namespace kth;
using namespace kth::domain::chain;
using namespace kth::domain::machine;
using namespace kth::domain::wallet;
using namespace kth::domain::wallet::cashtoken;

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

namespace {

hash_digest const category_a = {{
    0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}};

hash_digest const parent_tx_a = {{
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};

hash_digest const parent_tx_b = {{
    0xCC, 0xDD, 0xEE, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

// Deterministic fake address. payment_address constructed from a short_hash
// never validates on-chain but round-trips cleanly through the wallet API,
// which is all these unit tests need (they exercise the transaction-builder
// logic, not address derivation).
payment_address make_addr(uint8_t fill) {
    short_hash h{};
    h.fill(fill);
    return payment_address{h, payment_address::mainnet_p2kh};
}

utxo make_bch_utxo(hash_digest const& parent, uint32_t index, uint64_t amount) {
    return utxo{output_point{parent, index}, amount, std::nullopt};
}

utxo make_ft_utxo(hash_digest const& parent, uint32_t index,
                  hash_digest const& cat, uint64_t ft, uint64_t sats = 800) {
    return utxo{
        output_point{parent, index},
        sats,
        token_data_t{cat, fungible{amount_t(ft)}}
    };
}

utxo make_nft_utxo(hash_digest const& parent, uint32_t index,
                   hash_digest const& cat,
                   capability_t cap,
                   data_chunk commitment,
                   uint64_t sats = 800) {
    return utxo{
        output_point{parent, index},
        sats,
        token_data_t{cat, non_fungible{cap, std::move(commitment)}}
    };
}

utxo make_both_utxo(hash_digest const& parent, uint32_t index,
                    hash_digest const& cat, uint64_t ft,
                    capability_t cap, data_chunk commitment,
                    uint64_t sats = 800) {
    return utxo{
        output_point{parent, index},
        sats,
        token_data_t{cat, both_kinds{
            fungible{amount_t(ft)},
            non_fungible{cap, std::move(commitment)}
        }}
    };
}

} // anonymous namespace

// ===========================================================================
// Commitment helper — tested against the reference vectors from
// Bitcoin Cash Node's `scriptnum_tests.cpp`.
// ===========================================================================

TEST_CASE("encode_nft_number: BCHN reference vectors", "[cashtoken_minting]") {
    // Sourced from bitcoin-cash-node/src/test/scriptnum_tests.cpp,
    // the canonical CScriptNum serialization suite.
    REQUIRE(encode_nft_number(0).value()    == data_chunk{});
    REQUIRE(encode_nft_number(1).value()    == data_chunk{0x01});
    REQUIRE(encode_nft_number(-1).value()   == data_chunk{0x81});
    REQUIRE(encode_nft_number(127).value()  == data_chunk{0x7f});
    REQUIRE(encode_nft_number(128).value()  == (data_chunk{0x80, 0x00}));
    REQUIRE(encode_nft_number(256).value()  == (data_chunk{0x00, 0x01}));
    REQUIRE(encode_nft_number(-255).value() == (data_chunk{0xff, 0x80}));
}

TEST_CASE("encode_nft_number: power-of-two boundaries", "[cashtoken_minting]") {
    // Just-under / just-over boundaries where the encoded length grows
    // by one byte. These are the exact transitions BCHN exercises in
    // its `offsets` loop.
    REQUIRE(encode_nft_number(0x7f)->size()   == 1);   // 127 = 7-bit max positive
    REQUIRE(encode_nft_number(0x80)->size()   == 2);   // 128 forces a new byte
    REQUIRE(encode_nft_number(0x7fff)->size() == 2);   // 15-bit max positive
    REQUIRE(encode_nft_number(0x8000)->size() == 3);   // 32768 forces a new byte
    REQUIRE(encode_nft_number(0xffff)->size() == 3);
    REQUIRE(encode_nft_number(0x10000)->size() == 3);
}

TEST_CASE("encode_nft_number: realistic NFT counter sizes", "[cashtoken_minting]") {
    // A counter commitment for a 10k-NFT collection fits in 2 bytes;
    // a 1M-NFT collection in 3 bytes — well under the 40-byte Descartes
    // cap and the 128-byte Leibniz cap.
    REQUIRE(encode_nft_number(500)->size()       == 2);
    REQUIRE(encode_nft_number(9999)->size()      == 2);
    REQUIRE(encode_nft_number(1'000'000)->size() == 3);
}

TEST_CASE("encode_nft_number: round-trip sanity for negatives", "[cashtoken_minting]") {
    // NFT counters are positive in practice, but the encoder must
    // preserve the full script-number range so callers can use it for
    // any commitment value they can derive from an integer.
    REQUIRE(encode_nft_number(-128).value() == (data_chunk{0x80, 0x80}));
    REQUIRE(encode_nft_number(-256).value() == (data_chunk{0x00, 0x81}));
}

TEST_CASE("encode_nft_number: INT64_MIN rejected with distinct error", "[cashtoken_minting]") {
    // `INT64_MIN` cannot be negated in two's complement and therefore
    // has no valid VM-number encoding. The function must surface this
    // as an error, NOT collide with the empty-chunk encoding of `0`.
    auto const r = encode_nft_number(std::numeric_limits<int64_t>::min());
    REQUIRE( ! r.has_value());
}

TEST_CASE("encode_nft_number: INT64_MAX encodes to a valid 8-byte vector", "[cashtoken_minting]") {
    // The upper bound of the VM-number range must round-trip.
    auto const r = encode_nft_number(std::numeric_limits<int64_t>::max());
    REQUIRE(r.has_value());
    REQUIRE(r->size() == 8);
}

// ===========================================================================
// Output factories
// ===========================================================================

TEST_CASE("create_ft_output produces a fungible-only token output", "[cashtoken_minting]") {
    auto out = create_ft_output(make_addr(0xAB), category_a, 1000, 1500);
    REQUIRE(out.token_data().has_value());
    auto const& td = out.token_data().value();
    REQUIRE(td.id == category_a);
    REQUIRE(is_fungible_only(td));
    REQUIRE(get_amount(td) == 1000);
    REQUIRE(out.value() == 1500);
}

TEST_CASE("create_nft_output produces an NFT output with capability", "[cashtoken_minting]") {
    data_chunk commitment{0xDE, 0xAD, 0xBE, 0xEF};
    auto out = create_nft_output(
        make_addr(0xCD), category_a, capability_t::mut, commitment, 1000);
    REQUIRE(out.token_data().has_value());
    auto const& td = out.token_data().value();
    REQUIRE(td.id == category_a);
    REQUIRE(has_nft(td));
    REQUIRE(get_nft(td).capability == capability_t::mut);
    REQUIRE(get_nft(td).commitment == commitment);
}

TEST_CASE("create_combined_token_output carries both FT and NFT in one output", "[cashtoken_minting]") {
    data_chunk commitment{0x01};
    auto out = create_combined_token_output(
        make_addr(0x01), category_a, 5000, capability_t::minting, commitment, 1000);
    REQUIRE(out.token_data().has_value());
    auto const& td = out.token_data().value();
    REQUIRE(get_amount(td) == 5000);
    REQUIRE(has_nft(td));
    REQUIRE(get_nft(td).capability == capability_t::minting);
}

// ===========================================================================
// prepare_genesis_utxo
// ===========================================================================

TEST_CASE("prepare_genesis_utxo produces a TX with output 0 at the requested amount", "[cashtoken_minting]") {
    prepare_genesis_params params{};
    params.utxo = make_bch_utxo(parent_tx_a, 3, 50000);
    params.destination = make_addr(0x11);
    params.satoshis = 10000;

    auto result = prepare_genesis_utxo(params);
    REQUIRE(result.has_value());
    REQUIRE(result->signing_indices.size() == 1);
    REQUIRE(result->signing_indices[0] == 0);

    auto const& outs = result->transaction.outputs();
    REQUIRE(outs.size() >= 1);
    REQUIRE(outs[0].value() == 10000);
    REQUIRE( ! outs[0].token_data().has_value());
}

TEST_CASE("prepare_genesis_utxo rejects dust-level requested amount", "[cashtoken_minting]") {
    prepare_genesis_params params{};
    params.utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.satoshis = 100;                // below bch_dust_limit

    auto result = prepare_genesis_utxo(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("prepare_genesis_utxo rejects insufficient input funds", "[cashtoken_minting]") {
    prepare_genesis_params params{};
    params.utxo = make_bch_utxo(parent_tx_a, 0, 1000);   // barely above the request
    params.destination = make_addr(0x11);
    params.satoshis = 10000;

    auto result = prepare_genesis_utxo(params);
    REQUIRE( ! result.has_value());
}

// ===========================================================================
// create_token_genesis
// ===========================================================================

TEST_CASE("create_token_genesis requires the genesis UTXO to spend output index 0", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, /*index=*/3, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = 1000;

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_invalid_category);
}

TEST_CASE("create_token_genesis requires at least one payload kind", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    // neither ft_amount nor nft set

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_genesis creates a pure-fungible genesis output", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = 1'000'000;

    auto result = create_token_genesis(params);
    REQUIRE(result.has_value());
    REQUIRE(result->category_id == parent_tx_a);
    REQUIRE(result->transaction.outputs().size() >= 1);

    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(o0.token_data().has_value());
    auto const& td = o0.token_data().value();
    REQUIRE(td.id == parent_tx_a);
    REQUIRE(is_fungible_only(td));
    REQUIRE(get_amount(td) == 1'000'000);
}

TEST_CASE("create_token_genesis creates a minting NFT (nft-only, minting capability)", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.nft = nft_spec{capability_t::minting, data_chunk{0x00}};

    auto result = create_token_genesis(params);
    REQUIRE(result.has_value());
    auto const& td = result->transaction.outputs()[0].token_data().value();
    REQUIRE(has_nft(td));
    REQUIRE(is_minting_nft(td));
}

TEST_CASE("create_token_genesis creates FT + minting NFT combined output", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = 1000;
    params.nft = nft_spec{capability_t::minting, data_chunk{0x00}};

    auto result = create_token_genesis(params);
    REQUIRE(result.has_value());
    auto const& td = result->transaction.outputs()[0].token_data().value();
    REQUIRE(has_nft(td));
    REQUIRE(is_minting_nft(td));
    REQUIRE(get_amount(td) == 1000);
}

TEST_CASE("create_token_genesis rejects oversized NFT commitment (>40 bytes)", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.nft = nft_spec{
        capability_t::none,
        data_chunk(41, 0xAB)
    };

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_commitment_oversized);
}

TEST_CASE("create_token_genesis rejects zero fungible amount", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = 0;

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_amount_overflow);
}

TEST_CASE("create_token_genesis rejects fungible amount above INT64_MAX", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_amount_overflow);
}

// ===========================================================================
// create_token_mint
// ===========================================================================

TEST_CASE("create_token_mint requires the source UTXO to carry a minting NFT", "[cashtoken_minting]") {
    token_mint_params params{};
    // Give the source UTXO an immutable NFT instead of a minting one.
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::none, data_chunk{0x01});
    params.nfts.push_back({make_addr(0x22), data_chunk{0x02}, capability_t::none, 800});
    params.minting_destination = make_addr(0x11);

    auto result = create_token_mint(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_invalid_category);
}

TEST_CASE("create_token_mint rejects an empty NFT list", "[cashtoken_minting]") {
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 10000);
    params.minting_destination = make_addr(0x11);
    // params.nfts intentionally empty

    auto result = create_token_mint(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_mint preserves the minting NFT in output 0 and mints one NFT", "[cashtoken_minting]") {
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 10000);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.nfts.push_back({make_addr(0x22), data_chunk{0x01}, capability_t::none, 800});

    auto result = create_token_mint(params);
    REQUIRE(result.has_value());
    REQUIRE(result->minted_output_indices.size() == 1);
    REQUIRE(result->minted_output_indices[0] == 1);

    // Output 0: minting NFT preserved
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(o0.token_data().has_value());
    REQUIRE(is_minting_nft(o0.token_data().value()));

    // Output 1: the minted NFT (immutable)
    auto const& o1 = result->transaction.outputs()[1];
    REQUIRE(o1.token_data().has_value());
    REQUIRE(is_immutable_nft(o1.token_data().value()));
    REQUIRE(get_nft(o1.token_data().value()).commitment == data_chunk{0x01});
}

TEST_CASE("create_token_mint batches many NFTs into consecutive outputs", "[cashtoken_minting]") {
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 100000);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 500000));
    for (uint8_t i = 0; i < 10; ++i) {
        params.nfts.push_back({
            make_addr(0xA0 + i),
            data_chunk{uint8_t(i + 1)},
            capability_t::none,
            800
        });
    }

    auto result = create_token_mint(params);
    REQUIRE(result.has_value());
    REQUIRE(result->minted_output_indices.size() == 10);
    // minting NFT at 0, minted NFTs at 1..10
    REQUIRE(result->minted_output_indices.front() == 1);
    REQUIRE(result->minted_output_indices.back() == 10);
}

TEST_CASE("create_token_mint updates the minting NFT commitment when requested", "[cashtoken_minting]") {
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 10000);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.new_minting_commitment = data_chunk{0x05};
    params.nfts.push_back({make_addr(0x22), data_chunk{0x01}, capability_t::none, 800});

    auto result = create_token_mint(params);
    REQUIRE(result.has_value());
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(get_nft(o0.token_data().value()).commitment == data_chunk{0x05});
}

// ===========================================================================
// create_token_transfer
// ===========================================================================

TEST_CASE("create_token_transfer sends a fungible amount and emits token change", "[cashtoken_minting]") {
    token_transfer_params params{};
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.ft_amount = 300;
    params.token_change_address = make_addr(0x11);
    params.bch_change_address = make_addr(0x11);

    auto result = create_token_transfer(params);
    REQUIRE(result.has_value());

    // output 0: destination with 300 FT
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(get_amount(o0.token_data().value()) == 300);

    // output 1: token change with 700 FT (1000 - 300)
    auto const& o1 = result->transaction.outputs()[1];
    REQUIRE(get_amount(o1.token_data().value()) == 700);
}

TEST_CASE("create_token_transfer rejects insufficient fungible supply", "[cashtoken_minting]") {
    token_transfer_params params{};
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 0, category_a, 100));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.ft_amount = 1000;
    params.token_change_address = make_addr(0x11);

    auto result = create_token_transfer(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_fungible_insufficient);
}

TEST_CASE("create_token_transfer rejects zero fungible amount", "[cashtoken_minting]") {
    token_transfer_params params{};
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 0, category_a, 1000));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.ft_amount = 0;
    params.token_change_address = make_addr(0x11);

    auto result = create_token_transfer(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_amount_overflow);
}

TEST_CASE("create_token_transfer rejects fungible amount above INT64_MAX", "[cashtoken_minting]") {
    // Values above `INT64_MAX` are outside the VM token-amount range;
    // `make_fungible` would otherwise accept the out-of-range value and
    // the transaction would only fail at broadcast-time token
    // validation — returning a clear API error here is the difference
    // between a loud compile-time-quality rejection and a silent
    // unrelayable TX handed back to the caller.
    token_transfer_params params{};
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 0, category_a, 1000));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.ft_amount = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
    params.token_change_address = make_addr(0x11);

    auto result = create_token_transfer(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_amount_overflow);
}

TEST_CASE("create_token_transfer rejects UTXOs from different categories", "[cashtoken_minting]") {
    hash_digest category_b = {{0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02}};

    token_transfer_params params{};
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 0, category_a, 100));
    params.token_utxos.push_back(make_ft_utxo(parent_tx_a, 1, category_b, 100));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.ft_amount = 50;
    params.token_change_address = make_addr(0x11);

    auto result = create_token_transfer(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_invalid_category);
}

TEST_CASE("create_token_transfer transfers an NFT identified by commitment", "[cashtoken_minting]") {
    data_chunk target{0xDE, 0xAD};
    token_transfer_params params{};
    params.token_utxos.push_back(make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::none, target));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.nft = nft_spec{capability_t::none, target};

    auto result = create_token_transfer(params);
    REQUIRE(result.has_value());
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(has_nft(o0.token_data().value()));
    REQUIRE(get_nft(o0.token_data().value()).commitment == target);
}

TEST_CASE("create_token_transfer rejects NFT not present in inputs", "[cashtoken_minting]") {
    token_transfer_params params{};
    params.token_utxos.push_back(make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::none, data_chunk{0x01}));
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.nft = nft_spec{capability_t::none, data_chunk{0xFF}};

    auto result = create_token_transfer(params);
    REQUIRE( ! result.has_value());
}

// ===========================================================================
// create_token_burn
// ===========================================================================

TEST_CASE("create_token_burn destroys all fungible tokens when amount == balance", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 1000;

    auto result = create_token_burn(params);
    REQUIRE(result.has_value());
    // After burning everything, output 0 has no token payload.
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE( ! o0.token_data().has_value());
}

TEST_CASE("create_token_burn destroys part of the FT supply and keeps the rest", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 300;

    auto result = create_token_burn(params);
    REQUIRE(result.has_value());
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(o0.token_data().has_value());
    REQUIRE(get_amount(o0.token_data().value()) == 700);
}

TEST_CASE("create_token_burn refuses to burn more FT than the UTXO holds", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 100, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 1000;

    auto result = create_token_burn(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_fungible_insufficient);
}

TEST_CASE("create_token_burn burns the NFT while keeping the FT side of a both-kinds UTXO", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_both_utxo(
        parent_tx_a, 0, category_a, 500,
        capability_t::minting, data_chunk{0x00}, 10000);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_nft = true;

    auto result = create_token_burn(params);
    REQUIRE(result.has_value());
    auto const& o0 = result->transaction.outputs()[0];
    REQUIRE(o0.token_data().has_value());
    // FT survives, NFT gone
    REQUIRE(is_fungible_only(o0.token_data().value()));
    REQUIRE(get_amount(o0.token_data().value()) == 500);
}

TEST_CASE("create_token_burn refuses a no-op burn request", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.destination = make_addr(0x22);
    // neither burn_ft_amount nor burn_nft

    auto result = create_token_burn(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_burn attaches an OP_RETURN message when provided", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 1000;
    params.message = std::string{"final supply close"};

    auto result = create_token_burn(params);
    REQUIRE(result.has_value());
    REQUIRE(result->transaction.outputs().size() >= 2);
    auto const& op_return = result->transaction.outputs()[1];
    REQUIRE(op_return.value() == 0);
    REQUIRE( ! op_return.token_data().has_value());
}

// ===========================================================================
// High-level: create_ft
// ===========================================================================

TEST_CASE("create_ft builds a simple FT genesis", "[cashtoken_minting]") {
    ft_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.total_supply = 21'000'000;

    auto result = create_ft(params);
    REQUIRE(result.has_value());
    auto const& td = result->transaction.outputs()[0].token_data().value();
    REQUIRE(is_fungible_only(td));
    REQUIRE(get_amount(td) == 21'000'000);
}

TEST_CASE("create_ft with_minting_nft produces both FT and minting NFT", "[cashtoken_minting]") {
    ft_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.total_supply = 1'000'000;
    params.with_minting_nft = true;

    auto result = create_ft(params);
    REQUIRE(result.has_value());
    auto const& td = result->transaction.outputs()[0].token_data().value();
    REQUIRE(is_minting_nft(td));
    REQUIRE(get_amount(td) == 1'000'000);
}

// ===========================================================================
// High-level: create_nft_collection
// ===========================================================================

TEST_CASE("create_nft_collection partitions NFTs into batches and returns a plan", "[cashtoken_minting]") {
    nft_collection_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 1'000'000);
    params.creator_address = make_addr(0x11);
    for (uint8_t i = 0; i < 12; ++i) {
        params.nfts.push_back({data_chunk{uint8_t(i + 1)}, std::nullopt});
    }
    params.batch_size = 5;

    auto result = create_nft_collection(params);
    REQUIRE(result.has_value());
    REQUIRE(result->category_id == parent_tx_a);
    // 12 NFTs / 5 per batch = 3 batches (5 + 5 + 2)
    REQUIRE(result->batches.size() == 3);
    REQUIRE(result->batches[0].mint_requests.size() == 5);
    REQUIRE(result->batches[1].mint_requests.size() == 5);
    REQUIRE(result->batches[2].mint_requests.size() == 2);
    REQUIRE(result->final_burn == true);
}

TEST_CASE("create_nft_collection honours keep_minting_token", "[cashtoken_minting]") {
    nft_collection_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 1'000'000);
    params.creator_address = make_addr(0x11);
    params.nfts.push_back({data_chunk{0x01}, std::nullopt});
    params.keep_minting_token = true;

    auto result = create_nft_collection(params);
    REQUIRE(result.has_value());
    REQUIRE(result->final_burn == false);
}

TEST_CASE("create_nft_collection rejects oversized commitments upfront", "[cashtoken_minting]") {
    nft_collection_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 1'000'000);
    params.creator_address = make_addr(0x11);
    // second NFT has an oversized commitment; the whole plan must be rejected.
    params.nfts.push_back({data_chunk{0x01}, std::nullopt});
    params.nfts.push_back({data_chunk(41, 0xAB), std::nullopt});

    auto result = create_nft_collection(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_commitment_oversized);
}

TEST_CASE("create_nft_collection rejects an empty NFT list", "[cashtoken_minting]") {
    nft_collection_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 1'000'000);
    params.creator_address = make_addr(0x11);

    auto result = create_nft_collection(params);
    REQUIRE( ! result.has_value());
}

// ===========================================================================
// Safety guards added in response to review feedback
// ===========================================================================

TEST_CASE("prepare_genesis_utxo rejects a default-constructed destination", "[cashtoken_minting]") {
    prepare_genesis_params params{};
    params.utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    // params.destination intentionally default-constructed
    params.satoshis = 10000;

    auto result = prepare_genesis_utxo(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_genesis rejects token-carrying fee UTXOs (would burn tokens)", "[cashtoken_minting]") {
    token_genesis_params params{};
    params.genesis_utxo = make_bch_utxo(parent_tx_a, 0, 50000);
    params.destination = make_addr(0x11);
    params.ft_amount = 1000;
    // A token-bearing UTXO in fee_utxos would be silently burned.
    params.fee_utxos.push_back(make_ft_utxo(parent_tx_b, 1, category_a, 500));

    auto result = create_token_genesis(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_mint rejects an oversized new_minting_commitment", "[cashtoken_minting]") {
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 10000);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.new_minting_commitment = data_chunk(41, 0xAB);  // 41 > 40 byte limit
    params.nfts.push_back({make_addr(0x22), data_chunk{0x01}, capability_t::none, 800});

    auto result = create_token_mint(params);
    REQUIRE( ! result.has_value());
    REQUIRE(result.error() == kth::error::token_commitment_oversized);
}

TEST_CASE("create_token_mint accepts commitments above 40 bytes under Leibniz script flags", "[cashtoken_minting]") {
    // The 2026-May Leibniz upgrade raises the NFT commitment cap from
    // 40 to 128 bytes; it is gated by `script_flags::bch_loops`. When
    // the caller opts in via `script_flags`, a commitment that would
    // be rejected under Descartes rules must be accepted.
    token_mint_params params{};
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 10000);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.script_flags = script_flags::bch_loops;
    params.nfts.push_back({
        make_addr(0x22),
        data_chunk(80, 0xAA),           // 40 < 80 ≤ 128
        capability_t::none,
        800
    });

    auto result = create_token_mint(params);
    REQUIRE(result.has_value());
}

TEST_CASE("create_token_mint rejects a minting UTXO below the token dust limit", "[cashtoken_minting]") {
    token_mint_params params{};
    // Carrier BCH below token_dust_limit (800) would produce an
    // unrelayable preserved-minting output.
    params.minting_utxo = make_nft_utxo(
        parent_tx_a, 0, category_a, capability_t::minting, data_chunk{0x00}, 500);
    params.minting_destination = make_addr(0x11);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.nfts.push_back({make_addr(0x22), data_chunk{0x01}, capability_t::none, 800});

    auto result = create_token_mint(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_burn rejects a message above the OP_RETURN standardness limit", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 1000;
    params.message = std::string(max_op_return_payload_size + 1, 'X');

    auto result = create_token_burn(params);
    REQUIRE( ! result.has_value());
}

TEST_CASE("create_token_burn accepts a message at the OP_RETURN standardness boundary", "[cashtoken_minting]") {
    token_burn_params params{};
    params.token_utxo = make_ft_utxo(parent_tx_a, 0, category_a, 1000, 800);
    params.fee_utxos.push_back(make_bch_utxo(parent_tx_b, 0, 50000));
    params.destination = make_addr(0x22);
    params.burn_ft_amount = 1000;
    params.message = std::string(max_op_return_payload_size, 'X');

    auto result = create_token_burn(params);
    REQUIRE(result.has_value());
}
