// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <unistd.h>

#include <test_helpers.hpp>

#include <kth/blockchain.hpp>
#include <kth/database.hpp>

using namespace kth;
using namespace kth::blockchain;
using namespace kth::database;
using namespace kd::chain;

namespace {

std::filesystem::path fresh_utxoz_dir() {
    // Unique per-run path so concurrent test binaries don't clobber each other's db.
    auto const p = std::filesystem::temp_directory_path() /
        ("kth_utxoz_roundtrip_test_" + std::to_string(getpid()));
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    REQUIRE( ! ec);
    REQUIRE(std::filesystem::create_directories(p, ec));
    REQUIRE( ! ec);
    return p;
}

} // namespace

// Regression test for the full-mode UTXO write/read format mismatch.
//
// utxo_build stores UTXO values via process_compact_block_utxos ->
// apply_delta_raw, and get_utxo (validation / mempool prevout resolution) reads
// them back via utxoz_database::find -> utxo_entry::from_data. The two must agree
// on the byte layout of the value. Two mismatches (present since UTXO-Z was
// introduced) made EVERY full-mode UTXO lookup return the key but fail to decode:
//   1. field order: the writer put the fixed metadata before the output, while the
//      reader expects the output first;
//   2. output form: the writer stores the block's WIRE output bytes, while the
//      reader deserialized the output in non-wire form.
// Both are invisible until something reads UTXO-Z (IBD is merkle-only and never
// calls get_utxo), so it stayed latent.
#ifndef KTH_UTXOZ_REFERENCE_MODE
TEST_CASE("utxoz full-mode value round-trips write path -> find", "[utxoz][regression]") {
    utxoz_database db;
    REQUIRE(db.open(fresh_utxoz_dir(), true));

    // The output that will become a UTXO (value + a trivial script).
    output out;
    out.set_value(1234500);
    out.set_script(script{data_chunk{0x51}, false});   // OP_TRUE

    // Raw output bytes exactly as parse_utxo_block captures them from a block:
    // the WIRE serialization, which is what utxo_build stores.
    data_chunk raw(out.serialized_size(true));
    byte_writer writer(raw);
    REQUIRE(out.to_data(writer, true).has_value());

    hash_digest txid{};
    txid[0] = 0xAB;
    txid[31] = 0xCD;
    uint32_t const index = 0;

    // Build the compact block exactly as parse_utxo_block would for one output.
    utxo_compact_block block;
    utxo_compact_block::output_entry oe{
        utxoz::make_outpoint(std::span<uint8_t const, 32>(txid.data(), 32), index),
        std::span<uint8_t const>(raw.data(), raw.size()),
        /*coinbase*/ false,
        /*tx_start*/ 0u
    };
    block.outputs.push_back(oe);

    // Write path (blockchain): serialize + insert.
    auto delta_result = process_compact_block_utxos(
        block, /*height*/ 100u, /*mtp*/ 111u, /*file*/ 0, /*data_pos*/ 0u, nullptr);
    REQUIRE(delta_result.has_value());
    auto& delta = *delta_result;
    REQUIRE(db.apply_inserts_raw(delta.inserts) == result_code::success);

    // Read path (database), the same one get_utxo uses. Look the prevout up at a
    // later height than it was created (as validation does at the spending block).
    auto const found = db.find(point{txid, index}, 200000);
    REQUIRE(found.has_value());                        // both mismatches make this fail
    CHECK(found->output().value() == 1234500u);
    CHECK(found->height() == 100u);
    CHECK(found->median_time_past() == 111u);
    CHECK(found->coinbase() == false);
}
#endif // KTH_UTXOZ_REFERENCE_MODE
