// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/domain/chain/block.h>
#include <kth/capi/domain/chain/block_list.h>
#include <kth/capi/domain/chain/header.h>
#include <kth/capi/domain/chain/transaction_list.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"
#include "block_fixtures.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API BlockList - default construct is empty",
          "[C-API BlockList]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_domain_chain_block_list_count(list) == 0u);
    kth_domain_chain_block_list_destruct(list);
}

TEST_CASE("C-API BlockList - destruct null is safe",
          "[C-API BlockList]") {
    kth_domain_chain_block_list_destruct(NULL);
}

// ---------------------------------------------------------------------------
// push_back / count / nth
// ---------------------------------------------------------------------------

TEST_CASE("C-API BlockList - push_back increases count",
          "[C-API BlockList]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    kth_block_mut_t blk = make_block();

    kth_domain_chain_block_list_push_back(list, blk);
    REQUIRE(kth_domain_chain_block_list_count(list) == 1u);

    kth_domain_chain_block_list_push_back(list, blk);
    REQUIRE(kth_domain_chain_block_list_count(list) == 2u);

    kth_domain_chain_block_destruct(blk);
    kth_domain_chain_block_list_destruct(list);
}

TEST_CASE("C-API BlockList - nth returns borrowed element",
          "[C-API BlockList]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    kth_block_mut_t blk = make_block();
    kth_domain_chain_block_list_push_back(list, blk);
    kth_domain_chain_block_destruct(blk);

    kth_block_const_t elem = kth_domain_chain_block_list_nth(list, 0);
    REQUIRE(elem != NULL);
    REQUIRE(kth_domain_chain_block_is_valid_merkle_root(elem) != 0);

    kth_domain_chain_block_list_destruct(list);
}

// ---------------------------------------------------------------------------
// assign_at / erase
// ---------------------------------------------------------------------------

TEST_CASE("C-API BlockList - assign_at replaces element",
          "[C-API BlockList]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    kth_block_mut_t blk = make_block();
    kth_block_mut_t def = make_minimal_block();

    kth_domain_chain_block_list_push_back(list, def);
    REQUIRE(kth_domain_chain_block_equals(kth_domain_chain_block_list_nth(list, 0), blk) == 0);

    kth_domain_chain_block_list_assign_at(list, 0, blk);
    REQUIRE(kth_domain_chain_block_equals(kth_domain_chain_block_list_nth(list, 0), blk) != 0);

    kth_domain_chain_block_destruct(def);
    kth_domain_chain_block_destruct(blk);
    kth_domain_chain_block_list_destruct(list);
}

TEST_CASE("C-API BlockList - erase removes element",
          "[C-API BlockList]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    kth_block_mut_t blk = make_block();

    kth_domain_chain_block_list_push_back(list, blk);
    kth_domain_chain_block_list_push_back(list, blk);
    REQUIRE(kth_domain_chain_block_list_count(list) == 2u);

    kth_domain_chain_block_list_erase(list, 0);
    REQUIRE(kth_domain_chain_block_list_count(list) == 1u);

    kth_domain_chain_block_destruct(blk);
    kth_domain_chain_block_list_destruct(list);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API BlockList - nth out of bounds aborts",
          "[C-API BlockList][precondition]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    KTH_EXPECT_ABORT(kth_domain_chain_block_list_nth(list, 0));
    kth_domain_chain_block_list_destruct(list);
}

TEST_CASE("C-API BlockList - erase out of bounds aborts",
          "[C-API BlockList][precondition]") {
    kth_block_list_mut_t list = kth_domain_chain_block_list_construct_default();
    KTH_EXPECT_ABORT(kth_domain_chain_block_list_erase(list, 0));
    kth_domain_chain_block_list_destruct(list);
}
