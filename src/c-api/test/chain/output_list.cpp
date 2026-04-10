// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/output.h>
#include <kth/capi/chain/output_list.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static uint8_t const kScriptBody[20] = {
    0xec, 0xe4, 0x24, 0xa6, 0xbb, 0x6d, 0xdf, 0x4d,
    0xb5, 0x92, 0xc0, 0xfa, 0xed, 0x60, 0x68, 0x50,
    0x47, 0xa3, 0x61, 0xb1
};

static kth_output_mut_t make_output(void) {
    kth_script_mut_t script = NULL;
    kth_error_code_t ec = kth_chain_script_construct_from_data(
        kScriptBody, sizeof(kScriptBody), 0, &script);
    REQUIRE(ec == kth_ec_success);
    kth_output_mut_t out = kth_chain_output_construct(50000ull, script, NULL);
    REQUIRE(out != NULL);
    kth_chain_script_destruct(script);
    return out;
}

TEST_CASE("C-API OutputList - default construct is empty",
          "[C-API OutputList]") {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_output_list_count(list) == 0u);
    kth_chain_output_list_destruct(list);
}

TEST_CASE("C-API OutputList - destruct null is safe",
          "[C-API OutputList]") {
    kth_chain_output_list_destruct(NULL);
}

TEST_CASE("C-API OutputList - push_back / count / nth",
          "[C-API OutputList]") {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    kth_output_mut_t out = make_output();

    kth_chain_output_list_push_back(list, out);
    REQUIRE(kth_chain_output_list_count(list) == 1u);

    kth_output_const_t elem = kth_chain_output_list_nth(list, 0);
    REQUIRE(elem != NULL);
    REQUIRE(kth_chain_output_is_valid(elem) != 0);

    kth_chain_output_destruct(out);
    kth_chain_output_list_destruct(list);
}

TEST_CASE("C-API OutputList - erase removes element",
          "[C-API OutputList]") {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    kth_output_mut_t out = make_output();
    kth_chain_output_list_push_back(list, out);
    kth_chain_output_list_push_back(list, out);
    REQUIRE(kth_chain_output_list_count(list) == 2u);
    kth_chain_output_list_erase(list, 0);
    REQUIRE(kth_chain_output_list_count(list) == 1u);
    kth_chain_output_destruct(out);
    kth_chain_output_list_destruct(list);
}

TEST_CASE("C-API OutputList - nth out of bounds aborts",
          "[C-API OutputList][precondition]") {
    kth_output_list_mut_t list = kth_chain_output_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_list_nth(list, 0));
    kth_chain_output_list_destruct(list);
}
