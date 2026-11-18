// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include <kth/blockchain/spent_outputs.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/databases/utxo_entry.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
using blockchain::output_of_stored_entry;
using blockchain::spent_outputs_with_heights;
using database::block_undo;
using database::result_code;
using database::spent_output;

namespace {

constexpr uint32_t block_height = 77;

hash_digest leading_byte(uint8_t first) {
    hash_digest hash{};
    hash.front() = first;
    return hash;
}

// A pay-to-public-key-hash script whose hash is `fill` twenty times.
chain::script script_of(uint8_t fill) {
    data_chunk bytes{0x76, 0xa9, 0x14};
    bytes.insert(bytes.end(), 20, fill);
    bytes.push_back(0x88);
    bytes.push_back(0xac);
    return chain::script{std::move(bytes), false};
}

chain::input spend(hash_digest const& hash, uint32_t index) {
    return chain::input{chain::output_point{hash, index}, chain::script{}, max_input_sequence};
}

chain::output pay(uint64_t value, uint8_t fill, chain::token_data_opt token = {}) {
    return chain::output{value, script_of(fill), std::move(token)};
}

chain::transaction coinbase_tx() {
    data_chunk one{0x51};
    return chain::transaction{1, 0,
        chain::input::list{chain::input{chain::output_point{null_hash, chain::point::null_index},
            chain::script{std::move(one), false}, max_input_sequence}},
        chain::output::list{pay(5000, 0xc0)}};
}

chain::block block_of(chain::transaction::list txs) {
    chain::header const header{4, null_hash, null_hash, 1293623863, 0, 0};
    return chain::block{header, std::move(txs)};
}

utxoz::raw_outpoint key_of(hash_digest const& hash, uint32_t index) {
    utxoz::raw_outpoint key{};
    std::memcpy(key.data(), hash.data(), hash.size());
    key[32] = uint8_t(index & 0xff);
    key[33] = uint8_t((index >> 8) & 0xff);
    key[34] = uint8_t((index >> 16) & 0xff);
    key[35] = uint8_t((index >> 24) & 0xff);
    return key;
}

// The entry a full-mode undo record keeps for an output: the stored UTXO entry.
spent_output recorded(hash_digest const& hash, uint32_t index, chain::output const& output, uint32_t height) {
    auto const data = database::utxo_entry{output, height, 1293620000, false}.to_data();
    return spent_output{key_of(hash, index), std::vector<uint8_t>(data.begin(), data.end()), height};
}

std::expected<chain::output, result_code> read_full(spent_output const& entry) {
    return output_of_stored_entry(entry.value);
}

chain::token_data_opt fungible(uint64_t amount) {
    hash_digest category{};
    category.fill(0x22);
    return chain::token_data_opt{chain::make_fungible(category, amount)};
}

} // namespace

TEST_CASE("the output of a full-mode undo entry is read from the stored UTXO entry", "[spent_outputs]") {
    auto const plain = pay(1234, 0x11);
    auto const tokens = pay(1000, 0x12, fungible(40));

    auto const plain_entry = recorded(leading_byte(1), 0, plain, 700000);
    auto const token_entry = recorded(leading_byte(1), 1, tokens, 700001);

    auto const read_plain = output_of_stored_entry(plain_entry.value);
    REQUIRE(read_plain);
    CHECK(*read_plain == plain);

    auto const read_tokens = output_of_stored_entry(token_entry.value);
    REQUIRE(read_tokens);
    CHECK(*read_tokens == tokens);

    // Cut inside the script: not an output.
    auto const cut = std::span<uint8_t const>(plain_entry.value).first(12);
    auto const read_cut = output_of_stored_entry(cut);
    REQUIRE( ! read_cut);
    CHECK(read_cut.error() == result_code::other);
}

TEST_CASE("the spent outputs follow the inputs of the block, whatever the order of the record", "[spent_outputs]") {
    auto const funding = leading_byte(0x99);
    auto const first = pay(100, 0x11, fungible(100));
    auto const second = pay(200, 0x12);
    auto const third = pay(300, 0x13);
    auto const unrelated = pay(400, 0x14);

    chain::transaction const two_inputs{2, 0,
        chain::input::list{spend(funding, 0), spend(funding, 1)},
        chain::output::list{pay(250, 0x21)}};
    chain::transaction const one_input{2, 0,
        chain::input::list{spend(funding, 2)},
        chain::output::list{pay(290, 0x22)}};
    auto const block = block_of({coinbase_tx(), two_inputs, one_input});

    // Newest input first, and one entry no input names: what a block that
    // re-creates an earlier coinbase keeps for the entry it displaced.
    block_undo undo;
    undo.spent.push_back(recorded(funding, 2, third, 30));
    undo.spent.push_back(recorded(leading_byte(0x77), 0, unrelated, 5));
    undo.spent.push_back(recorded(funding, 1, second, 20));
    undo.spent.push_back(recorded(funding, 0, first, 10));

    auto const spent = spent_outputs_with_heights(block, block_height, undo, read_full);
    REQUIRE(spent);
    CHECK(spent->outputs == chain::output::list{first, second, third});
    // The height each was created at is the one its entry holds, not the block's.
    CHECK(spent->heights == std::vector<uint32_t>{10, 20, 30});
}

TEST_CASE("an output created and spent in the same block is taken from the block", "[spent_outputs]") {
    auto const funding = leading_byte(0x99);
    auto const old = pay(900, 0x11);

    chain::transaction const creator{2, 0,
        chain::input::list{spend(funding, 0)},
        chain::output::list{pay(500, 0x31), pay(390, 0x32)}};
    chain::transaction const spender{2, 0,
        chain::input::list{spend(creator.hash(), 1)},
        chain::output::list{pay(380, 0x33)}};

    // Only the output that existed before the block is in its record.
    block_undo undo;
    undo.spent.push_back(recorded(funding, 0, old, 10));

    SECTION("the creating transaction stands first") {
        auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), creator, spender}), block_height,
            undo, read_full);
        REQUIRE(spent);
        CHECK(spent->outputs == chain::output::list{old, creator.outputs()[1]});
        // The one the block created itself was created at the block's height.
        CHECK(spent->heights == std::vector<uint32_t>{10, block_height});
    }

    SECTION("the creating transaction stands after the one that spends its output") {
        auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), spender, creator}), block_height,
            undo, read_full);
        REQUIRE(spent);
        CHECK(spent->outputs == chain::output::list{creator.outputs()[1], old});
        CHECK(spent->heights == std::vector<uint32_t>{block_height, 10});
    }
}

TEST_CASE("an input whose output is in neither the record nor the block is reported", "[spent_outputs]") {
    auto const funding = leading_byte(0x99);
    chain::transaction const creator{2, 0,
        chain::input::list{spend(funding, 0)},
        chain::output::list{pay(500, 0x31)}};

    block_undo undo;
    undo.spent.push_back(recorded(funding, 0, pay(900, 0x11), 10));

    SECTION("an output of no known transaction") {
        chain::transaction const stray{2, 0,
            chain::input::list{spend(leading_byte(0x55), 0)},
            chain::output::list{pay(1, 0x41)}};
        auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), creator, stray}), block_height,
            undo, read_full);
        REQUIRE( ! spent);
        CHECK(spent.error() == result_code::key_not_found);
    }

    SECTION("an output index the transaction of the block does not have") {
        chain::transaction const past_the_end{2, 0,
            chain::input::list{spend(creator.hash(), 1)},
            chain::output::list{pay(1, 0x41)}};
        auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), creator, past_the_end}),
            block_height, undo, read_full);
        REQUIRE( ! spent);
        CHECK(spent.error() == result_code::key_not_found);
    }

    SECTION("an empty record") {
        auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), creator}), block_height,
            block_undo{}, read_full);
        REQUIRE( ! spent);
        CHECK(spent.error() == result_code::key_not_found);
    }
}

TEST_CASE("an entry that cannot be read fails the whole block", "[spent_outputs]") {
    auto const funding = leading_byte(0x99);
    chain::transaction const tx{2, 0,
        chain::input::list{spend(funding, 0), spend(funding, 1)},
        chain::output::list{pay(500, 0x31)}};

    block_undo undo;
    undo.spent.push_back(recorded(funding, 0, pay(900, 0x11), 10));
    undo.spent.push_back(recorded(funding, 1, pay(800, 0x12), 11));

    size_t reads = 0;
    auto const spent = spent_outputs_with_heights(block_of({coinbase_tx(), tx}), block_height, undo,
        [&](spent_output const& entry) -> std::expected<chain::output, result_code> {
            ++reads;
            if (entry.height == 11) {
                return std::unexpected(result_code::db_corrupt);
            }
            return output_of_stored_entry(entry.value);
        });
    REQUIRE( ! spent);
    CHECK(spent.error() == result_code::db_corrupt);
    CHECK(reads == 2);
}

TEST_CASE("a block of one transaction spends nothing and reads no entry", "[spent_outputs]") {
    // The coinbase input names no output, and a record is not looked at.
    block_undo undo;
    undo.spent.push_back(recorded(null_hash, chain::point::null_index, pay(1, 0x11), 1));

    size_t reads = 0;
    auto const spent = spent_outputs_with_heights(block_of({coinbase_tx()}), block_height, undo,
        [&](spent_output const&) -> std::expected<chain::output, result_code> {
            ++reads;
            return std::unexpected(result_code::other);
        });
    REQUIRE(spent);
    CHECK(spent->outputs.empty());
    CHECK(spent->heights.empty());
    CHECK(reads == 0);
}
