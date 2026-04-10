// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/point.h>
#include <kth/capi/chain/point_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static kth_hash_t const kHash = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
}};

static kth_point_mut_t make_point(void) {
    kth_point_mut_t pt = kth_chain_point_construct(kHash, 42);
    REQUIRE(pt != NULL);
    return pt;
}

TEST_CASE("C-API PointList - default construct is empty",
          "[C-API PointList]") {
    kth_point_list_mut_t list = kth_chain_point_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_point_list_count(list) == 0u);
    kth_chain_point_list_destruct(list);
}

TEST_CASE("C-API PointList - destruct null is safe",
          "[C-API PointList]") {
    kth_chain_point_list_destruct(NULL);
}

TEST_CASE("C-API PointList - push_back / count / nth",
          "[C-API PointList]") {
    kth_point_list_mut_t list = kth_chain_point_list_construct_default();
    kth_point_mut_t pt = make_point();

    kth_chain_point_list_push_back(list, pt);
    REQUIRE(kth_chain_point_list_count(list) == 1u);

    kth_point_const_t elem = kth_chain_point_list_nth(list, 0);
    REQUIRE(elem != NULL);
    REQUIRE(kth_chain_point_is_valid(elem) != 0);

    kth_chain_point_destruct(pt);
    kth_chain_point_list_destruct(list);
}

TEST_CASE("C-API PointList - erase removes element",
          "[C-API PointList]") {
    kth_point_list_mut_t list = kth_chain_point_list_construct_default();
    kth_point_mut_t pt = make_point();
    kth_chain_point_list_push_back(list, pt);
    kth_chain_point_list_push_back(list, pt);
    REQUIRE(kth_chain_point_list_count(list) == 2u);
    kth_chain_point_list_erase(list, 0);
    REQUIRE(kth_chain_point_list_count(list) == 1u);
    kth_chain_point_destruct(pt);
    kth_chain_point_list_destruct(list);
}

TEST_CASE("C-API PointList - nth out of bounds aborts",
          "[C-API PointList][precondition]") {
    kth_point_list_mut_t list = kth_chain_point_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_point_list_nth(list, 0));
    kth_chain_point_list_destruct(list);
}
