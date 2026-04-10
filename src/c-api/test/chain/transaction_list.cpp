// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/transaction.h>
#include <kth/capi/chain/transaction_list.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

static kth_transaction_mut_t make_tx(void) {
    kth_transaction_mut_t tx = kth_chain_transaction_construct_default();
    REQUIRE(tx != NULL);
    return tx;
}

TEST_CASE("C-API TransactionList - default construct is empty",
          "[C-API TransactionList]") {
    kth_transaction_list_mut_t list = kth_chain_transaction_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_transaction_list_count(list) == 0u);
    kth_chain_transaction_list_destruct(list);
}

TEST_CASE("C-API TransactionList - destruct null is safe",
          "[C-API TransactionList]") {
    kth_chain_transaction_list_destruct(NULL);
}

TEST_CASE("C-API TransactionList - push_back / count / nth",
          "[C-API TransactionList]") {
    kth_transaction_list_mut_t list = kth_chain_transaction_list_construct_default();
    kth_transaction_mut_t tx = make_tx();

    kth_chain_transaction_list_push_back(list, tx);
    REQUIRE(kth_chain_transaction_list_count(list) == 1u);

    kth_chain_transaction_list_push_back(list, tx);
    REQUIRE(kth_chain_transaction_list_count(list) == 2u);

    kth_transaction_const_t elem = kth_chain_transaction_list_nth(list, 0);
    REQUIRE(elem != NULL);

    kth_chain_transaction_destruct(tx);
    kth_chain_transaction_list_destruct(list);
}

TEST_CASE("C-API TransactionList - erase removes element",
          "[C-API TransactionList]") {
    kth_transaction_list_mut_t list = kth_chain_transaction_list_construct_default();
    kth_transaction_mut_t tx = make_tx();
    kth_chain_transaction_list_push_back(list, tx);
    kth_chain_transaction_list_push_back(list, tx);
    REQUIRE(kth_chain_transaction_list_count(list) == 2u);
    kth_chain_transaction_list_erase(list, 0);
    REQUIRE(kth_chain_transaction_list_count(list) == 1u);
    kth_chain_transaction_destruct(tx);
    kth_chain_transaction_list_destruct(list);
}

TEST_CASE("C-API TransactionList - nth out of bounds aborts",
          "[C-API TransactionList][precondition]") {
    kth_transaction_list_mut_t list = kth_chain_transaction_list_construct_default();
    KTH_EXPECT_ABORT(kth_chain_transaction_list_nth(list, 0));
    kth_chain_transaction_list_destruct(list);
}
