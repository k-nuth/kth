// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/token_tests.cpp (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b):
//   prefix_token_encoding_json_test_vectors_valid
//   prefix_token_encoding_json_test_vectors_invalid
// over src/test/data/token_tests_prefix_{valid,invalid}.json.
// Original: Copyright (c) 2022-present The Bitcoin developers, MIT license.
//
// BCHN unwraps the token prefix of a scriptPubKey with UnwrapScriptPubKey; in
// Knuth that happens while an output is deserialized, so each vector is fed to
// output::from_data as an output whose script is exactly the prefix bytes
// (the vectors omit the locking bytecode that would follow the prefix).
//
// Not run here, tracked in test/bchn_non_vm/coverage.json with their evidence:
//   valid 37 (a 1001-byte commitment: Knuth refuses to parse it, BCHN parses it
//   and rejects it later by the commitment limit);
//   invalid 1, 2, 3 (a script of fewer than 34 bytes starting with 0xef: BCHN
//   production also keeps it as a plain script, so only the post-activation
//   rejection of such outputs is in question);
//   invalid 45, 46, 53, 54 (a zero or out-of-range amount: Knuth parses and
//   round-trips them, as invalid token data, instead of failing to parse).

#include <test_helpers.hpp>

#include "bchn_token_vectors_data.hpp"

#include <string>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

// value (8 bytes, 0) + compact size of the script (1 or 3 bytes) + script
data_chunk output_bytes_with_script(std::string const& script_hex) {
    auto script = decode_base16(script_hex);
    REQUIRE(script);
    REQUIRE(script->size() <= 0xffff);
    data_chunk bytes(8, 0x00);
    if (script->size() < 0xfd) {
        bytes.push_back(uint8_t(script->size()));
    } else {
        bytes.push_back(0xfd);
        bytes.push_back(uint8_t(script->size()));
        bytes.push_back(uint8_t(script->size() >> 8));
    }
    bytes.insert(bytes.end(), script->begin(), script->end());
    return bytes;
}

} // namespace

TEST_CASE("BCHN token_tests - prefix_token_encoding_json_test_vectors_valid", "[bchn_non_vm][token]") {
    REQUIRE( ! bchn_token_vectors::valid().empty());
    for (auto const& v : bchn_token_vectors::valid()) {
        DYNAMIC_SECTION("valid vector " << v.index << " " << v.prefix) {
            auto const bytes = output_bytes_with_script(v.prefix);
            byte_reader reader(bytes);
            auto parsed = chain::output::from_data(reader, true);
            REQUIRE(parsed);
            REQUIRE(parsed->token_data().has_value());
            auto const& td = *parsed->token_data();

            // Re-serialization is identical
            CHECK(kth::to_data_chunk(*parsed, true) == bytes);

            // Category (JSON is big endian, memory is little endian)
            CHECK(encode_hash(td.id) == v.category);

            // Amount
            auto const amount = std::stoll(v.amount);
            CHECK(chain::get_amount(td) == amount);

            // NFT
            CHECK(chain::has_nft(td) == v.has_nft);
            if (v.has_nft) {
                CHECK(encode_base16(chain::get_nft_commitment(td)) == std::string(v.commitment));
                auto const cap = chain::get_nft_capability(td);
                std::string const name = cap == chain::capability_t::none ? "none"
                    : cap == chain::capability_t::mut ? "mutable" : "minting";
                CHECK(name == v.capability);
            }
        }
    }
}

TEST_CASE("BCHN token_tests - prefix_token_encoding_json_test_vectors_invalid", "[bchn_non_vm][token]") {
    REQUIRE( ! bchn_token_vectors::invalid().empty());
    for (auto const& v : bchn_token_vectors::invalid()) {
        DYNAMIC_SECTION("invalid vector " << v.index << " " << v.prefix << " (" << v.error << ")") {
            auto const bytes = output_bytes_with_script(v.prefix);
            byte_reader reader(bytes);
            auto parsed = chain::output::from_data(reader, true);
            CHECK( ! parsed);
        }
    }
}
