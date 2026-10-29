// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_TEST_TRANSACTION_SIZE_FIXTURE_HPP
#define KTH_TEST_TRANSACTION_SIZE_FIXTURE_HPP
#include <test_helpers.hpp>
#include "regtest_miner.hpp"
namespace kth::test {
using namespace kth::domain::chain;
// A P2SH OP_TRUE spend with ordinary P2PKH outputs and at most 40 bytes of
// push-only OP_RETURN. Padding never enlarges scriptSig or a spendable script.
inline transaction sized_transaction(size_t size, bool coinbase = false,
                                     std::optional<output_point> previous = std::nullopt,
                                     std::optional<script> input_script = std::nullopt) {
    using domain::machine::opcode;
    using domain::machine::operation;
    input::list ins;
    ins.emplace_back(coinbase ? output_point{null_hash, point::null_index}
                             : previous.value_or(output_point{bitcoin_hash(data_chunk{1}), 0}),
                     input_script.value_or(script(operation::list{operation(data_chunk{0x51}, false)})),
                     0xffffffff);
    output::list outs;
    auto const count = (size - 70) / 34;
    for (size_t i = 0; i < count; ++i) {
        outs.emplace_back(1000, test::miner_script(), std::nullopt);
    }
    outs.emplace_back(0, script(operation::list{operation(opcode::return_)}), std::nullopt);
    transaction tx{1, 0, ins, outs};
    auto const current = tx.serialized_size(true);
    REQUIRE(current <= size);
    operation::list padding{operation(opcode::return_)};
    for (size_t i = current; i < size; ++i) padding.emplace_back(opcode::push_size_0);
    outs.back().set_script(script(std::move(padding)));
    tx = transaction{1, 0, std::move(ins), std::move(outs)};
    REQUIRE(tx.serialized_size(true) == size);
    REQUIRE(to_data_chunk(tx, true).size() == size);
    REQUIRE(tx.outputs().back().script().serialized_size(false) <= 40);
    return tx;
}

} // namespace kth::test
#endif
