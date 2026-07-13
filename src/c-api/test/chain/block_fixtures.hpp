// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_
#define KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/chain/block.h>
#include <kth/capi/chain/header.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/chain/transaction_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

// The canonical valid test block (genesis mainnet).
inline kth_block_mut_t make_block(void) {
    kth_block_mut_t blk = kth_chain_block_genesis_mainnet();
    REQUIRE(blk != NULL);
    return blk;
}

// A minimal block: a non-zero header and a single transaction (a block always
// has at least one). Distinct from genesis, so it doubles as the "other" block
// for inequality tests.
inline kth_block_mut_t make_minimal_block(void) {
    static kth_hash_t const zero_hash = {{0}};
    // Minimal serializable tx: version=1, no inputs, no outputs, locktime=0.
    static uint8_t const minimal_tx[10] = {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    kth_header_mut_t h = kth_chain_header_construct(1u, &zero_hash, &zero_hash, 0u, 0u, 0u);
    kth_transaction_mut_t tx = NULL;
    REQUIRE(kth_chain_transaction_construct_from_data(
        minimal_tx, sizeof(minimal_tx), 1, &tx) == kth_ec_success);
    kth_transaction_list_mut_t txs = kth_chain_transaction_list_construct_default();
    kth_chain_transaction_list_push_back(txs, tx);
    kth_block_mut_t blk = NULL;
    kth_error_code_t ec = kth_chain_block_create(h, txs, &blk);
    REQUIRE(ec == kth_ec_success);
    REQUIRE(blk != NULL);
    kth_chain_transaction_destruct(tx);
    kth_chain_header_destruct(h);
    kth_chain_transaction_list_destruct(txs);
    return blk;
}

#endif // KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_
