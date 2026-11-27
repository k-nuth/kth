// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/config/network.h>
#include <kth/capi/domain/chain/block.h>
#include <kth/capi/domain/chain/input_list.h>
#include <kth/capi/domain/chain/output.h>
#include <kth/capi/domain/chain/output_list.h>
#include <kth/capi/domain/chain/script.h>
#include <kth/capi/domain/chain/transaction.h>
#include <kth/capi/domain/machine/operation.h>
#include <kth/capi/domain/machine/operation_list.h>
#include <kth/capi/error.h>
#include <kth/capi/primitives.h>
#include <kth/capi/vm/program.h>

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("C-API network genesis preserves the legacy factory", "[C-API][domain-api]") {
    kth_block_mut_t block = kth_domain_chain_block_genesis(kth_network_chipnet);
    kth_block_mut_t legacy = kth_domain_chain_block_genesis_chipnet();
    REQUIRE(block != NULL);
    REQUIRE(legacy != NULL);
    CHECK(kth_domain_chain_block_equals(block, legacy));
    CHECK(kth_domain_chain_block_has_forbidden_coinbase_shape(block));
    CHECK(kth_domain_chain_block_has_forbidden_coinbase_shape_at_height(block, kth_network_chipnet, 0));
    CHECK_FALSE(kth_domain_chain_block_has_forbidden_coinbase_shape_at_height(block, kth_network_mainnet, 0));
    kth_domain_chain_block_destruct(legacy);
    kth_domain_chain_block_destruct(block);
}

#endif // KTH_CURRENCY_BCH

TEST_CASE("C-API script parsing reports failure without publishing a handle", "[C-API][domain-api]") {
    kth_script_mut_t script = NULL;
    REQUIRE(kth_domain_chain_script_parse_from("1 2 add", &script) == kth_ec_success);
    REQUIRE(script != NULL);
    kth_program_mut_t program = kth_vm_program_construct_from_script(script);
    REQUIRE(program != NULL);
    kth_operation_list_const_t operations = kth_vm_program_operations(program);
    CHECK(kth_domain_machine_operation_list_count(operations) == 3);
    // Borrowed operations belong to program; script outlives program.
    kth_vm_program_destruct(program);
    kth_domain_chain_script_destruct(script);
    script = NULL;
    CHECK(kth_domain_chain_script_parse_from("not_an_opcode", &script) != kth_ec_success);
    CHECK(script == NULL);
}

TEST_CASE("C-API operation take_data owns its result and resets its source", "[C-API][domain-api]") {
    uint8_t const wire[] = { 3, 0xaa, 0xbb, 0xcc };
    kth_operation_mut_t operation = NULL;
    REQUIRE(kth_domain_machine_operation_construct_from_data(wire, sizeof(wire), &operation) == kth_ec_success);
    kth_size_t size = 0;
    uint8_t* data = kth_domain_machine_operation_take_data(operation, &size);
    CHECK(size == 3);
    CHECK_FALSE(kth_domain_machine_operation_is_valid(operation));
    kth_domain_machine_operation_destruct(operation);
    REQUIRE(data != NULL);
    CHECK(memcmp(data, wire + 1, 3) == 0);
    kth_core_destruct_array(data);
}

TEST_CASE("C-API push-data size preserves out slots on truncated input", "[C-API][domain-api]") {
    uint8_t const size_bytes[] = { 0x34, 0x12 };
    uint32_t size = 7;
    CHECK(kth_domain_machine_operation_read_data_size(kth_opcode_push_two_size, size_bytes, 2, &size) == kth_ec_success);
    CHECK(size == 0x1234);
    size = 7;
    CHECK(kth_domain_machine_operation_read_data_size(kth_opcode_push_two_size, size_bytes, 1, &size) != kth_ec_success);
    CHECK(size == 7);
}

TEST_CASE("C-API token validation exposes malformed token outputs", "[C-API][domain-api]") {
    uint8_t const prefix[] = { 0xef };
    kth_script_mut_t script = NULL;
    REQUIRE(kth_domain_chain_script_construct_from_data(prefix, sizeof(prefix), 0, &script) == kth_ec_success);
    kth_output_mut_t output = kth_domain_chain_output_construct(1, script, NULL);
    kth_output_list_mut_t outputs = kth_domain_chain_output_list_construct_default();
    kth_input_list_mut_t inputs = kth_domain_chain_input_list_construct_default();
    kth_domain_chain_output_list_push_back(outputs, output);
    kth_transaction_mut_t tx = kth_domain_chain_transaction_construct(2, 0, inputs, outputs);
    CHECK(kth_domain_chain_transaction_validate_tokens(tx, kth_script_flags_no_rules) == kth_ec_success);
#if defined(KTH_CURRENCY_BCH)
    CHECK(kth_domain_chain_transaction_validate_tokens(tx, kth_script_flags_bch_tokens) == kth_ec_token_unparseable_output);
#endif // KTH_CURRENCY_BCH
    CHECK(kth_domain_chain_transaction_validate_token_birth(tx, kth_script_flags_no_rules, kth_network_mainnet) == kth_ec_success);
    kth_domain_chain_transaction_destruct(tx);
    kth_domain_chain_input_list_destruct(inputs);
    kth_domain_chain_output_list_destruct(outputs);
    kth_domain_chain_output_destruct(output);
    kth_domain_chain_script_destruct(script);
}
