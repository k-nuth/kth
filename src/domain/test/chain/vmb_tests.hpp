// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_TEST_VMB_TESTS_HPP_
#define KTH_DOMAIN_TEST_VMB_TESTS_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure/utility/data.hpp>

#ifndef REQUIRE_MESSAGE
#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)
#endif
#ifndef CHECK_MESSAGE
#define CHECK_MESSAGE(cond, msg) do { INFO(msg); CHECK(cond); } while((void)0, 0)
#endif

namespace kth::test {

struct vmb_test_case {
    std::string ident;
    std::string description;
    std::string tx_hex;
    std::string utxos_hex;
    uint32_t input_num;
    domain::script_flags_t flags;
    bool expect_pass;
    bool script_only;
};

inline
void run_vmb_test(vmb_test_case const& test) {
    using namespace kth::domain;
    using namespace kth::domain::chain;

    // Decode the transaction
    auto const tx_data = decode_base16(test.tx_hex);
    REQUIRE_MESSAGE(bool(tx_data), "Failed to decode tx hex for " + test.ident);

    byte_reader tx_reader(*tx_data);
    auto tx_result = transaction::from_data(tx_reader, true);
    REQUIRE_MESSAGE(bool(tx_result), "Failed to parse tx for " + test.ident);
    auto tx = std::move(*tx_result);

    // Decode the UTXOs
    auto const utxos_data = decode_base16(test.utxos_hex);
    REQUIRE_MESSAGE(bool(utxos_data), "Failed to decode utxos hex for " + test.ident);

    // Parse UTXOs: varint count, then count * CTxOut (8-byte value LE + varint script len + script)
    byte_reader utxo_reader(*utxos_data);
    auto const utxo_count_exp = utxo_reader.read_variable_little_endian();
    REQUIRE_MESSAGE(bool(utxo_count_exp), "Failed to read UTXO count for " + test.ident);
    auto const utxo_count = *utxo_count_exp;

    REQUIRE_MESSAGE(utxo_count == tx.inputs().size(),
        "UTXO count mismatch for " + test.ident +
        ": got " + std::to_string(utxo_count) +
        ", expected " + std::to_string(tx.inputs().size()));

    std::vector<output> utxos;
    utxos.reserve(utxo_count);
    for (uint64_t i = 0; i < utxo_count; ++i) {
        auto utxo_result = output::from_data(utxo_reader);
        REQUIRE_MESSAGE(bool(utxo_result), "Failed to parse UTXO " + std::to_string(i) + " for " + test.ident);
        utxos.push_back(std::move(*utxo_result));
    }

    // Populate the validation cache for each input with its corresponding UTXO.
    // This is needed for native introspection opcodes (OP_UTXOTOKENCATEGORY, etc.)
    // which read token data from tx.inputs()[i].previous_output().validation.cache.
    for (size_t i = 0; i < utxos.size(); ++i) {
        tx.inputs()[i].previous_output().validation.cache = utxos[i];
    }

    // Verify the specified input
    auto const idx = test.input_num;
    REQUIRE_MESSAGE(idx < tx.inputs().size(),
        "Input index " + std::to_string(idx) + " out of range for " + test.ident);

    // Structural checks (no context needed).
    auto ec = tx.check(one_million_bytes_block, false /*transaction_pool*/);

    // Contextual checks (prevout cache is populated above).
    if (ec == error::success) {
        // Use large height/mtp so sequence locks and finality checks don't reject valid transactions.
        ec = tx.accept(test.flags, 1'000'000 /*height*/, 1'700'000'000 /*median_time_past*/,
                       0 /*max_sigops*/, false /*is_under_checkpoint*/,
                       false /*transaction_pool*/);
    }

    // Script verification for the specified input.
    if (ec == error::success) {
        auto const& input_script = tx.inputs()[idx].script();
        auto const& prevout_script = utxos[idx].script();
        auto const value = utxos[idx].value();
        ec = verify(tx, idx, test.flags, input_script, prevout_script, value);
    }

    if (test.expect_pass) {
        CHECK_MESSAGE(ec == error::success,
            "VMB " + test.ident + ": expected success but got error " +
            std::to_string(static_cast<int>(ec.value())) +
            " desc: " + test.description);
    } else {
        CHECK_MESSAGE(ec != error::success,
            "VMB " + test.ident + ": expected failure but got success" +
            " desc: " + test.description);
    }
}

} // namespace kth::test

using kth::test::run_vmb_test;

#endif // KTH_DOMAIN_TEST_VMB_TESTS_HPP_
