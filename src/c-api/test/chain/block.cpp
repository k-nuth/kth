// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Note: This file is .cpp only to use Catch2 (which is C++).
// All code under test uses exclusively the C-API — no C++ domain types.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>

#include <kth/capi/chain/block.h>
#include <kth/capi/chain/header.h>
#include <kth/capi/chain/transaction_list.h>
#include <kth/capi/hash.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// construct / destruct
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - construct default is invalid", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_construct_default();
    REQUIRE(kth_chain_block_is_valid(block) == 0);
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// genesis blocks
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - genesis mainnet valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();
    REQUIRE(kth_chain_block_is_valid(block) != 0);
    REQUIRE(kth_chain_block_serialized_size(block, 1) == 285u);

    kth_header_const_t header = kth_chain_block_header(block);
    REQUIRE(kth_chain_header_serialized_size(header, 1) == 80u);

    kth_hash_t merkle_root = kth_chain_block_generate_merkle_root(block);
    kth_hash_t header_merkle = kth_chain_header_merkle(header);
    REQUIRE(kth_hash_equal(merkle_root, header_merkle));

    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - genesis testnet valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_testnet();
    REQUIRE(kth_chain_block_is_valid(block) != 0);

    kth_header_const_t header = kth_chain_block_header(block);
    kth_hash_t merkle_root = kth_chain_block_generate_merkle_root(block);
    kth_hash_t header_merkle = kth_chain_header_merkle(header);
    REQUIRE(kth_hash_equal(merkle_root, header_merkle));

    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - genesis regtest valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_regtest();
    REQUIRE(kth_chain_block_is_valid(block) != 0);

    kth_header_const_t header = kth_chain_block_header(block);
    kth_hash_t merkle_root = kth_chain_block_generate_merkle_root(block);
    kth_hash_t header_merkle = kth_chain_header_merkle(header);
    REQUIRE(kth_hash_equal(merkle_root, header_merkle));

    kth_chain_block_destruct(block);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("C-API Block - genesis testnet4 valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_testnet4();
    REQUIRE(kth_chain_block_is_valid(block) != 0);
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - genesis scalenet valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_scalenet();
    REQUIRE(kth_chain_block_is_valid(block) != 0);
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - genesis chipnet valid structure", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_chipnet();
    REQUIRE(kth_chain_block_is_valid(block) != 0);
    kth_chain_block_destruct(block);
}
#endif

// ---------------------------------------------------------------------------
// from_data / to_data round-trip
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - from data insufficient bytes fails", "[C-API Block]") {
    uint8_t data[10] = {};
    kth_block_mut_t out_block = nullptr;
    kth_error_code_t ec = kth_chain_block_from_data(data, 10, 1, &out_block);
    REQUIRE(ec != kth_ec_success);
    REQUIRE(out_block == nullptr);
}

TEST_CASE("C-API Block - from data genesis mainnet roundtrip", "[C-API Block]") {
    kth_block_mut_t genesis = kth_chain_block_genesis_mainnet();
    kth_size_t size = 0;
    uint8_t const* raw = kth_chain_block_to_data(genesis, 1, &size);
    REQUIRE(size == 285u);

    kth_block_mut_t block = nullptr;
    kth_error_code_t ec = kth_chain_block_from_data(raw, size, 1, &block);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(block != nullptr);
    REQUIRE(kth_chain_block_is_valid(block) != 0);
    REQUIRE(kth_chain_block_serialized_size(block, 1) == 285u);

    kth_header_const_t header = kth_chain_block_header(block);
    kth_hash_t merkle_root = kth_chain_block_generate_merkle_root(block);
    kth_hash_t header_merkle = kth_chain_header_merkle(header);
    REQUIRE(kth_hash_equal(merkle_root, header_merkle));

    free(const_cast<uint8_t*>(raw));
    kth_chain_block_destruct(block);
    kth_chain_block_destruct(genesis);
}

// ---------------------------------------------------------------------------
// hash
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - hash returns header hash", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_construct_default();
    kth_hash_t block_hash = kth_chain_block_hash(block);

    kth_header_const_t header = kth_chain_block_header(block);
    kth_hash_t header_hash = kth_chain_header_hash(header);

    REQUIRE(kth_hash_equal(block_hash, header_hash));
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - hash_out matches hash", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();

    kth_hash_t hash1 = kth_chain_block_hash(block);
    kth_hash_t hash2;
    kth_chain_block_hash_out(block, &hash2);

    REQUIRE(kth_hash_equal(hash1, hash2));
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// merkle root
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - generate merkle root empty block matches null hash", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_construct_default();
    kth_hash_t merkle = kth_chain_block_generate_merkle_root(block);
    REQUIRE(kth_hash_is_null(merkle));
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - generate merkle root out matches", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();

    kth_hash_t merkle1 = kth_chain_block_generate_merkle_root(block);
    kth_hash_t merkle2;
    kth_chain_block_generate_merkle_root_out(block, &merkle2);

    REQUIRE(kth_hash_equal(merkle1, merkle2));
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// is_valid_merkle_root
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - is valid merkle root uninitialized returns true", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_construct_default();
    REQUIRE(kth_chain_block_is_valid_merkle_root(block) != 0);
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - is valid merkle root genesis returns true", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();
    REQUIRE(kth_chain_block_is_valid_merkle_root(block) != 0);
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// subsidy (static)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - subsidy at height 0", "[C-API Block]") {
    REQUIRE(kth_chain_block_subsidy(0) == 5000000000ull);
}

TEST_CASE("C-API Block - subsidy at height 210000", "[C-API Block]") {
    REQUIRE(kth_chain_block_subsidy(210000) == 2500000000ull);
}

// ---------------------------------------------------------------------------
// locator_size (static)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - locator size zero backoff", "[C-API Block]") {
    REQUIRE(kth_chain_block_locator_size(7) == 8u);
}

TEST_CASE("C-API Block - locator size positive backoff", "[C-API Block]") {
    REQUIRE(kth_chain_block_locator_size(138) == 18u);
}

// ---------------------------------------------------------------------------
// is_distinct_transaction_set
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - is distinct transaction set empty true", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_construct_default();
    REQUIRE(kth_chain_block_is_distinct_transaction_set(block) != 0);
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// proof_str
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - proof str genesis mainnet", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();
    char const* proof = kth_chain_block_proof_str(block);
    REQUIRE(proof != nullptr);
    REQUIRE(std::strlen(proof) > 0);
    free(const_cast<char*>(proof));
    kth_chain_block_destruct(block);
}

// ---------------------------------------------------------------------------
// copy / equal
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - copy", "[C-API Block]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();
    kth_block_mut_t copy = kth_chain_block_copy(block);

    REQUIRE(kth_chain_block_is_valid(copy) != 0);
    REQUIRE(kth_chain_block_equal(block, copy) != 0);
    REQUIRE(kth_chain_block_serialized_size(copy, 1) == kth_chain_block_serialized_size(block, 1));

    kth_chain_block_destruct(copy);
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Block - equal", "[C-API Block]") {
    kth_block_mut_t a = kth_chain_block_genesis_mainnet();
    kth_block_mut_t b = kth_chain_block_genesis_mainnet();
    kth_block_mut_t c = kth_chain_block_construct_default();

    REQUIRE(kth_chain_block_equal(a, b) != 0);
    REQUIRE(kth_chain_block_equal(a, c) == 0);

    kth_chain_block_destruct(a);
    kth_chain_block_destruct(b);
    kth_chain_block_destruct(c);
}

// ---------------------------------------------------------------------------
// Preconditions (death tests via fork)
// ---------------------------------------------------------------------------

TEST_CASE("C-API Block - from_data null data aborts", "[C-API Block][precondition]") {
    KTH_EXPECT_ABORT({
        kth_block_mut_t out = nullptr;
        kth_chain_block_from_data(nullptr, 0, 1, &out);
    });
}

TEST_CASE("C-API Block - from_data null out_result aborts", "[C-API Block][precondition]") {
    uint8_t data[10] = {};
    KTH_EXPECT_ABORT(kth_chain_block_from_data(data, 10, 1, nullptr));
}

TEST_CASE("C-API Block - to_data null out_size aborts", "[C-API Block][precondition]") {
    kth_block_mut_t block = kth_chain_block_genesis_mainnet();
    KTH_EXPECT_ABORT(kth_chain_block_to_data(block, 1, nullptr));
    kth_chain_block_destruct(block);
}

TEST_CASE("C-API Transaction List - nth out of range aborts", "[C-API List][precondition]") {
    kth_transaction_list_mut_t list = kth_chain_transaction_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_transaction_list_nth(list, 9999));
    kth_chain_transaction_list_destruct(list);
}

