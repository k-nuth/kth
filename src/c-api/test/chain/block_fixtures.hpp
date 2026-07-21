// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_
#define KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/domain/chain/block.h>
#include <kth/capi/domain/chain/header.h>
#include <kth/capi/domain/chain/transaction.h>
#include <kth/capi/domain/chain/transaction_list.h>
#include <kth/capi/hash.h>
#include <kth/capi/primitives.h>

// The canonical valid test block (genesis mainnet).
inline kth_block_mut_t make_block(void) {
    kth_block_mut_t blk = kth_domain_chain_block_genesis_mainnet();
    REQUIRE(blk != NULL);
    return blk;
}

// A minimal block: a non-zero header, no transactions. Distinct from genesis,
// so it doubles as the "other" block for inequality tests.
inline kth_block_mut_t make_minimal_block(void) {
    static kth_hash_t const zero_hash = {{0}};
    kth_header_mut_t h = kth_domain_chain_header_construct(1u, &zero_hash, &zero_hash, 0u, 0u, 0u);
    kth_transaction_list_mut_t txs = kth_domain_chain_transaction_list_construct_default();
    kth_block_mut_t blk = kth_domain_chain_block_construct(h, txs);
    REQUIRE(blk != NULL);
    kth_domain_chain_header_destruct(h);
    kth_domain_chain_transaction_list_destruct(txs);
    return blk;
}

#endif // KTH_CAPI_TEST_CHAIN_BLOCK_FIXTURES_HPP_
