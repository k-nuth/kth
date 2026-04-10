// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/output_point_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static kth_hash_t const kHash = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_output_point_mut_t make_outpoint(void) {
    kth_output_point_mut_t op = kth_chain_output_point_construct_from_hash_index(kHash, 7);
    REQUIRE(op != NULL);
    return op;
}

TEST_CASE("C-API OutputPointList - default construct is empty",
          "[C-API OutputPointList]") {
    kth_output_point_list_mut_t list = kth_chain_output_point_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_output_point_list_count(list) == 0u);
    kth_chain_output_point_list_destruct(list);
}

TEST_CASE("C-API OutputPointList - destruct null is safe",
          "[C-API OutputPointList]") {
    kth_chain_output_point_list_destruct(NULL);
}

TEST_CASE("C-API OutputPointList - push_back / count / nth",
          "[C-API OutputPointList]") {
    kth_output_point_list_mut_t list = kth_chain_output_point_list_construct_default();
    kth_output_point_mut_t op = make_outpoint();

    kth_chain_output_point_list_push_back(list, op);
    REQUIRE(kth_chain_output_point_list_count(list) == 1u);

    kth_output_point_const_t elem = kth_chain_output_point_list_nth(list, 0);
    REQUIRE(elem != NULL);
    REQUIRE(kth_chain_output_point_is_valid(elem) != 0);

    kth_chain_output_point_destruct(op);
    kth_chain_output_point_list_destruct(list);
}

TEST_CASE("C-API OutputPointList - erase removes element",
          "[C-API OutputPointList]") {
    kth_output_point_list_mut_t list = kth_chain_output_point_list_construct_default();
    kth_output_point_mut_t op = make_outpoint();
    kth_chain_output_point_list_push_back(list, op);
    kth_chain_output_point_list_push_back(list, op);
    REQUIRE(kth_chain_output_point_list_count(list) == 2u);
    kth_chain_output_point_list_erase(list, 0);
    REQUIRE(kth_chain_output_point_list_count(list) == 1u);
    kth_chain_output_point_destruct(op);
    kth_chain_output_point_list_destruct(list);
}

TEST_CASE("C-API OutputPointList - nth out of bounds aborts",
          "[C-API OutputPointList][precondition]") {
    kth_output_point_list_mut_t list = kth_chain_output_point_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_output_point_list_nth(list, 0));
    kth_chain_output_point_list_destruct(list);
}
