// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is named .cpp solely so it can use Catch2 (which is C++).
// Everything inside the test bodies is plain C: no namespaces, no
// templates, no <chrono>, no std::*, no auto, no references, no constexpr.
// Only Catch2's TEST_CASE / REQUIRE macros are C++.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/output.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/token_capability.h>
#include <kth/capi/chain/utxo.h>
#include <kth/capi/chain/utxo_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>
#include <kth/capi/wallet/cashtoken_minting.h>
#include <kth/capi/wallet/payment_address.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static uint8_t const kParentTxA[32] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
};

static uint8_t const kCategoryA[32] = {
    0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01
};

// A valid mainnet P2PKH CashAddr for use across tests. Using a real,
// parse-able address keeps us clear of any internal preconditions in
// the downstream builders (which reject default-constructed addresses).
static char const* const kAddr1 =
    "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz";

static kth_payment_address_mut_t make_addr(void) {
    return kth_wallet_payment_address_construct_from_address(kAddr1);
}

// Build a plain BCH UTXO.
static kth_utxo_mut_t make_bch_utxo(uint8_t const* parent, uint32_t index, uint64_t amount) {
    kth_hash_t h;
    memcpy(h.hash, parent, 32);
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&h, index);
    kth_utxo_mut_t u = kth_chain_utxo_construct(op, amount, NULL);
    kth_chain_output_point_destruct(op);
    return u;
}

// ---------------------------------------------------------------------------
// encode_nft_number
// ---------------------------------------------------------------------------

TEST_CASE("C-API encode_nft_number - zero encodes to empty chunk",
          "[C-API cashtoken_minting]") {
    uint8_t* data = NULL;
    kth_size_t n = 0;
    kth_error_code_t ec = kth_wallet_cashtoken_encode_nft_number(0, &data, &n);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(n == 0u);
    kth_core_destruct_array(data);
}

TEST_CASE("C-API encode_nft_number - BCHN reference vectors",
          "[C-API cashtoken_minting]") {
    uint8_t* data = NULL;
    kth_size_t n = 0;

    // 1 -> {0x01}
    REQUIRE(kth_wallet_cashtoken_encode_nft_number(1, &data, &n) == kth_ec_success);
    REQUIRE(n == 1u);
    REQUIRE(data[0] == 0x01);
    kth_core_destruct_array(data);

    // 128 -> {0x80, 0x00}
    data = NULL;
    n = 0;
    REQUIRE(kth_wallet_cashtoken_encode_nft_number(128, &data, &n) == kth_ec_success);
    REQUIRE(n == 2u);
    REQUIRE(data[0] == 0x80);
    REQUIRE(data[1] == 0x00);
    kth_core_destruct_array(data);

    // 256 -> {0x00, 0x01}
    data = NULL;
    n = 0;
    REQUIRE(kth_wallet_cashtoken_encode_nft_number(256, &data, &n) == kth_ec_success);
    REQUIRE(n == 2u);
    REQUIRE(data[0] == 0x00);
    REQUIRE(data[1] == 0x01);
    kth_core_destruct_array(data);
}

TEST_CASE("C-API encode_nft_number - INT64_MIN rejected distinctly from zero",
          "[C-API cashtoken_minting]") {
    uint8_t* data = NULL;
    kth_size_t n = 0;
    kth_error_code_t ec = kth_wallet_cashtoken_encode_nft_number(INT64_MIN, &data, &n);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(data == NULL);
    REQUIRE(n == 0u);
}

// ---------------------------------------------------------------------------
// Output factories
// ---------------------------------------------------------------------------

TEST_CASE("C-API create_ft_output - returns a valid owned output",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t addr = make_addr();
    kth_output_mut_t out = kth_wallet_cashtoken_create_ft_output(addr, kCategoryA, 1000u, 1500u);
    REQUIRE(out != NULL);
    REQUIRE(kth_chain_output_value(out) == 1500u);
    kth_chain_output_destruct(out);
    kth_wallet_payment_address_destruct(addr);
}

TEST_CASE("C-API create_nft_output - returns a valid owned output",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t addr = make_addr();
    uint8_t const commitment[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    kth_output_mut_t out = kth_wallet_cashtoken_create_nft_output(
        addr, kCategoryA, kth_token_capability_mut, commitment, 4u, 1000u);
    REQUIRE(out != NULL);
    REQUIRE(kth_chain_output_value(out) == 1000u);
    kth_chain_output_destruct(out);
    kth_wallet_payment_address_destruct(addr);
}

TEST_CASE("C-API create_combined_token_output - returns a valid owned output",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t addr = make_addr();
    uint8_t const commitment[1] = {0x01};
    kth_output_mut_t out = kth_wallet_cashtoken_create_combined_token_output(
        addr, kCategoryA, 5000u, kth_token_capability_minting, commitment, 1u, 1000u);
    REQUIRE(out != NULL);
    kth_chain_output_destruct(out);
    kth_wallet_payment_address_destruct(addr);
}

// ---------------------------------------------------------------------------
// prepare_genesis_utxo
// ---------------------------------------------------------------------------

TEST_CASE("C-API prepare_genesis_utxo - happy path produces a result",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t dest = make_addr();
    kth_utxo_mut_t u = make_bch_utxo(kParentTxA, 3u, 50000u);

    kth_cashtoken_prepare_genesis_params_mut_t p =
        kth_wallet_cashtoken_prepare_genesis_params_construct_default();
    kth_wallet_cashtoken_prepare_genesis_params_set_utxo(p, u);
    kth_wallet_cashtoken_prepare_genesis_params_set_destination(p, dest);
    kth_wallet_cashtoken_prepare_genesis_params_set_satoshis(p, 10000u);

    kth_cashtoken_prepare_genesis_result_mut_t res = NULL;
    kth_error_code_t ec = kth_wallet_cashtoken_prepare_genesis_utxo(p, &res);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(res != NULL);

    REQUIRE(kth_wallet_cashtoken_prepare_genesis_result_signing_indices_count(res) == 1u);
    REQUIRE(kth_wallet_cashtoken_prepare_genesis_result_signing_index_nth(res, 0) == 0u);

    kth_wallet_cashtoken_prepare_genesis_result_destruct(res);
    kth_wallet_cashtoken_prepare_genesis_params_destruct(p);
    kth_chain_utxo_destruct(u);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API prepare_genesis_utxo - rejects dust-level satoshis",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t dest = make_addr();
    kth_utxo_mut_t u = make_bch_utxo(kParentTxA, 0u, 50000u);

    kth_cashtoken_prepare_genesis_params_mut_t p =
        kth_wallet_cashtoken_prepare_genesis_params_construct_default();
    kth_wallet_cashtoken_prepare_genesis_params_set_utxo(p, u);
    kth_wallet_cashtoken_prepare_genesis_params_set_destination(p, dest);
    kth_wallet_cashtoken_prepare_genesis_params_set_satoshis(p, 100u); // below dust

    kth_cashtoken_prepare_genesis_result_mut_t res = NULL;
    kth_error_code_t ec = kth_wallet_cashtoken_prepare_genesis_utxo(p, &res);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(res == NULL);

    kth_wallet_cashtoken_prepare_genesis_params_destruct(p);
    kth_chain_utxo_destruct(u);
    kth_wallet_payment_address_destruct(dest);
}

// ---------------------------------------------------------------------------
// create_token_genesis — pure FT
// ---------------------------------------------------------------------------

TEST_CASE("C-API create_token_genesis - FT-only returns category_id = parent txid",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t dest = make_addr();
    kth_utxo_mut_t u = make_bch_utxo(kParentTxA, 0u, 50000u);

    kth_cashtoken_token_genesis_params_mut_t p =
        kth_wallet_cashtoken_token_genesis_params_construct_default();
    kth_wallet_cashtoken_token_genesis_params_set_genesis_utxo(p, u);
    kth_wallet_cashtoken_token_genesis_params_set_destination(p, dest);
    kth_wallet_cashtoken_token_genesis_params_set_ft_amount(p, 1, 1000000u);
    kth_wallet_cashtoken_token_genesis_params_set_script_flags(p, 0u);

    kth_cashtoken_token_genesis_result_mut_t res = NULL;
    kth_error_code_t ec = kth_wallet_cashtoken_create_token_genesis(p, &res);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(res != NULL);

    uint8_t cat[32];
    kth_wallet_cashtoken_token_genesis_result_category_id(res, cat);
    REQUIRE(memcmp(cat, kParentTxA, 32) == 0);

    kth_wallet_cashtoken_token_genesis_result_destruct(res);
    kth_wallet_cashtoken_token_genesis_params_destruct(p);
    kth_chain_utxo_destruct(u);
    kth_wallet_payment_address_destruct(dest);
}

TEST_CASE("C-API create_token_genesis - rejects outpoint.index != 0",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t dest = make_addr();
    kth_utxo_mut_t u = make_bch_utxo(kParentTxA, 3u, 50000u); // index != 0

    kth_cashtoken_token_genesis_params_mut_t p =
        kth_wallet_cashtoken_token_genesis_params_construct_default();
    kth_wallet_cashtoken_token_genesis_params_set_genesis_utxo(p, u);
    kth_wallet_cashtoken_token_genesis_params_set_destination(p, dest);
    kth_wallet_cashtoken_token_genesis_params_set_ft_amount(p, 1, 1000u);
    kth_wallet_cashtoken_token_genesis_params_set_script_flags(p, 0u);

    kth_cashtoken_token_genesis_result_mut_t res = NULL;
    kth_error_code_t ec = kth_wallet_cashtoken_create_token_genesis(p, &res);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(res == NULL);

    kth_wallet_cashtoken_token_genesis_params_destruct(p);
    kth_chain_utxo_destruct(u);
    kth_wallet_payment_address_destruct(dest);
}

// ---------------------------------------------------------------------------
// create_nft_collection — high-level planner
// ---------------------------------------------------------------------------

TEST_CASE("C-API create_nft_collection - partitions into batches",
          "[C-API cashtoken_minting]") {
    kth_payment_address_mut_t creator = make_addr();
    kth_utxo_mut_t u = make_bch_utxo(kParentTxA, 0u, 1000000u);

    // Build a list of 12 NFT items (each with a 1-byte commitment).
    kth_cashtoken_nft_collection_item_list_mut_t items =
        kth_wallet_cashtoken_nft_collection_item_list_construct_default();
    for (uint8_t i = 0; i < 12; ++i) {
        uint8_t const commitment[1] = {(uint8_t)(i + 1)};
        kth_cashtoken_nft_collection_item_mut_t item =
            kth_wallet_cashtoken_nft_collection_item_construct(commitment, 1u, NULL);
        kth_wallet_cashtoken_nft_collection_item_list_push_back(items, item);
        kth_wallet_cashtoken_nft_collection_item_destruct(item);
    }

    kth_cashtoken_nft_collection_params_mut_t p =
        kth_wallet_cashtoken_nft_collection_params_construct_default();
    kth_wallet_cashtoken_nft_collection_params_set_genesis_utxo(p, u);
    kth_wallet_cashtoken_nft_collection_params_set_nfts(p, items);
    kth_wallet_cashtoken_nft_collection_params_set_creator_address(p, creator);
    kth_wallet_cashtoken_nft_collection_params_set_batch_size(p, 5u);
    kth_wallet_cashtoken_nft_collection_params_set_script_flags(p, 0u);

    kth_cashtoken_nft_collection_result_mut_t res = NULL;
    kth_error_code_t ec = kth_wallet_cashtoken_create_nft_collection(p, &res);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(res != NULL);

    // 12 / batch_size=5 -> 3 batches (5 + 5 + 2)
    REQUIRE(kth_wallet_cashtoken_nft_collection_result_batches_count(res) == 3u);

    // `final_burn` defaults to true (keep_minting_token == false).
    REQUIRE(kth_wallet_cashtoken_nft_collection_result_final_burn(res) != 0);

    // The category id equals `kParentTxA`.
    uint8_t cat[32];
    kth_wallet_cashtoken_nft_collection_result_category_id(res, cat);
    REQUIRE(memcmp(cat, kParentTxA, 32) == 0);

    // Batch 0 must have 5 requests, batch 2 must have 2.
    kth_cashtoken_nft_mint_request_list_const_t b0 =
        kth_wallet_cashtoken_nft_collection_result_batch_mint_requests(res, 0);
    REQUIRE(kth_wallet_cashtoken_nft_mint_request_list_count(b0) == 5u);

    kth_cashtoken_nft_mint_request_list_const_t b2 =
        kth_wallet_cashtoken_nft_collection_result_batch_mint_requests(res, 2);
    REQUIRE(kth_wallet_cashtoken_nft_mint_request_list_count(b2) == 2u);

    kth_wallet_cashtoken_nft_collection_result_destruct(res);
    kth_wallet_cashtoken_nft_collection_params_destruct(p);
    kth_wallet_cashtoken_nft_collection_item_list_destruct(items);
    kth_chain_utxo_destruct(u);
    kth_wallet_payment_address_destruct(creator);
}
