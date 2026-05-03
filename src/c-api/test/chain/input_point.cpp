// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// `input_point` is a typedef alias for `point` in the domain layer
// (`using input_point = point;`), so the underlying behavior is fully
// covered by `test/chain/point.cpp`. This file exercises the C-API
// surface specific to the alias — primarily `kth_chain_input_point_destruct`,
// which is the only owning operation a consumer of `kth_spend_fetch_handler_t`
// can call to release the `kth_input_point_mut_t` handed to them — plus
// enough smoke coverage on the rest of the alias-prefixed surface to
// prove the binding is wired through correctly.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/input_point.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static kth_hash_t const kHash = {{
    0x6f, 0xe2, 0x8c, 0x0a, 0xb6, 0xf1, 0xb3, 0x72,
    0xc1, 0xa6, 0xa2, 0x46, 0xae, 0x63, 0xf7, 0x4f,
    0x93, 0x1e, 0x83, 0x65, 0xe1, 0x5a, 0x08, 0x9c,
    0x68, 0xd6, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00
}};

// ---------------------------------------------------------------------------
// Destructor — the reason this binding exists; null-safe contract documented
// in the header.
// ---------------------------------------------------------------------------

TEST_CASE("C-API InputPoint - destruct null is a no-op", "[C-API InputPoint]") {
    kth_chain_input_point_destruct(NULL);
}

TEST_CASE("C-API InputPoint - destruct frees an allocated handle", "[C-API InputPoint]") {
    kth_input_point_mut_t ip = kth_chain_input_point_construct(&kHash, 7u);
    REQUIRE(ip != NULL);
    kth_chain_input_point_destruct(ip);
}

// ---------------------------------------------------------------------------
// Smoke coverage of the alias-prefixed surface.
// ---------------------------------------------------------------------------

TEST_CASE("C-API InputPoint - default construct is invalid", "[C-API InputPoint]") {
    kth_input_point_mut_t ip = kth_chain_input_point_construct_default();
    REQUIRE(kth_chain_input_point_is_valid(ip) == 0);
    kth_chain_input_point_destruct(ip);
}

TEST_CASE("C-API InputPoint - field constructor preserves hash and index", "[C-API InputPoint]") {
    kth_input_point_mut_t ip = kth_chain_input_point_construct(&kHash, 1234u);
    REQUIRE(kth_chain_input_point_is_valid(ip) != 0);
    REQUIRE(kth_chain_input_point_index(ip) == 1234u);
    REQUIRE(kth_hash_equal(kth_chain_input_point_hash(ip), kHash) != 0);
    kth_chain_input_point_destruct(ip);
}

TEST_CASE("C-API InputPoint - to_data / from_data roundtrip", "[C-API InputPoint]") {
    kth_input_point_mut_t expected = kth_chain_input_point_construct(&kHash, 53213u);

    kth_size_t size = 0;
    uint8_t* raw = kth_chain_input_point_to_data(expected, 1, &size);
    REQUIRE(size == 36u);
    REQUIRE(raw != NULL);

    kth_input_point_mut_t parsed = NULL;
    kth_error_code_t ec = kth_chain_input_point_construct_from_data(raw, size, 1, &parsed);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(parsed != NULL);
    REQUIRE(kth_chain_input_point_equals(expected, parsed) != 0);

    kth_core_destruct_array(raw);
    kth_chain_input_point_destruct(parsed);
    kth_chain_input_point_destruct(expected);
}

TEST_CASE("C-API InputPoint - copy preserves fields", "[C-API InputPoint]") {
    kth_input_point_mut_t original = kth_chain_input_point_construct(&kHash, 524342u);
    kth_input_point_mut_t copy = kth_chain_input_point_copy(original);

    REQUIRE(kth_chain_input_point_equals(original, copy) != 0);
    REQUIRE(kth_chain_input_point_index(copy) == 524342u);

    kth_chain_input_point_destruct(copy);
    kth_chain_input_point_destruct(original);
}

TEST_CASE("C-API InputPoint - null factory is is_null", "[C-API InputPoint]") {
    kth_input_point_mut_t ip = kth_chain_input_point_null();
    REQUIRE(kth_chain_input_point_is_null(ip) != 0);
    kth_chain_input_point_destruct(ip);
}

TEST_CASE("C-API InputPoint - satoshi_fixed_size is 36", "[C-API InputPoint]") {
    REQUIRE(kth_chain_input_point_satoshi_fixed_size() == 36u);
}
