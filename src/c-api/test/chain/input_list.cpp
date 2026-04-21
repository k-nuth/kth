// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/input.h>
#include <kth/capi/chain/input_list.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_hash_t const kPrevHash = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_input_mut_t make_input(void) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &script);
    REQUIRE(ec == kth_ec_success);
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(&kPrevHash, 0);
    REQUIRE(op != NULL);
    kth_input_mut_t in = kth_chain_input_construct(op, script, 0xffffffffu);
    REQUIRE(in != NULL);
    kth_chain_script_destruct(script);
    kth_chain_output_point_destruct(op);
    return in;
}

TEST_CASE("C-API InputList - default construct is empty",
          "[C-API InputList]") {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_input_list_count(list) == 0u);
    kth_chain_input_list_destruct(list);
}

TEST_CASE("C-API InputList - destruct null is safe",
          "[C-API InputList]") {
    kth_chain_input_list_destruct(NULL);
}

TEST_CASE("C-API InputList - push_back / count / nth",
          "[C-API InputList]") {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    kth_input_mut_t in = make_input();

    kth_chain_input_list_push_back(list, in);
    REQUIRE(kth_chain_input_list_count(list) == 1u);

    kth_input_const_t elem = kth_chain_input_list_nth(list, 0);
    REQUIRE(elem != NULL);
    REQUIRE(kth_chain_input_is_valid(elem) != 0);

    kth_chain_input_destruct(in);
    kth_chain_input_list_destruct(list);
}

TEST_CASE("C-API InputList - erase removes element",
          "[C-API InputList]") {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    kth_input_mut_t in = make_input();
    kth_chain_input_list_push_back(list, in);
    kth_chain_input_list_push_back(list, in);
    REQUIRE(kth_chain_input_list_count(list) == 2u);
    kth_chain_input_list_erase(list, 0);
    REQUIRE(kth_chain_input_list_count(list) == 1u);
    kth_chain_input_destruct(in);
    kth_chain_input_list_destruct(list);
}

TEST_CASE("C-API InputList - nth out of bounds aborts",
          "[C-API InputList][precondition]") {
    kth_input_list_mut_t list = kth_chain_input_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_input_list_nth(list, 0));
    kth_chain_input_list_destruct(list);
}
