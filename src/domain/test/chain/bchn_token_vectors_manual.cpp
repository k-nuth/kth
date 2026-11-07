// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The three hand-written vectors of Bitcoin Cash Node src/test/token_tests.cpp (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b) GenVectors_PrefixTokenEncoding_Valid / Invalid whose payload bytes are not in
// the JSON vector files already ported in bchn_token_prefix.cpp (the other 26 valid and 17 invalid ones are):
//   "2-byte immutable NFT; 253 fungible", "80-byte OUT OF CONSENSUS immutable NFT; 65536 fungible" and
//   "Has commitment length without NFT (1 fungible)" followed by a commitment.
// Original: Copyright (c) 2022 The Bitcoin Cash Node developers (MIT license).
// The script that follows each prefix is a P2PKH pattern; BCHN uses a random scriptPubKey. The TxOutCompression half of
// the original is BCHN storage format and is not ported.

#include <test_helpers.hpp>

#include <string>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

std::string const category_hex = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
std::string const spk_hex = "76a914" "00112233445566778899aabbccddeeff00112233" "88ac";

data_chunk output_with_script(std::string const& script_hex) {
    auto script = decode_base16(script_hex);
    REQUIRE(script);
    data_chunk bytes(8, 0x00);
    bytes.push_back(uint8_t(script->size() < 0xfd ? script->size() : 0xfd));
    if (script->size() >= 0xfd) {
        bytes.push_back(uint8_t(script->size()));
        bytes.push_back(uint8_t(script->size() >> 8));
    }
    bytes.insert(bytes.end(), script->begin(), script->end());
    return bytes;
}

void check_valid(std::string const& payload, int64_t amount, std::string const& commitment_hex) {
    auto const bytes = output_with_script("ef" + category_hex + payload + spk_hex);
    byte_reader reader(bytes);
    auto parsed = chain::output::from_data(reader, true);
    REQUIRE(parsed);
    REQUIRE(parsed->token_data().has_value());
    auto const& td = *parsed->token_data();
    CHECK(kth::to_data_chunk(*parsed, true) == bytes);
    CHECK(encode_hash(td.id) == category_hex);
    CHECK(chain::get_amount(td) == amount);
    CHECK(chain::has_nft(td));
    CHECK(chain::get_nft_capability(td) == chain::capability_t::none);
    CHECK(encode_base16(chain::get_nft_commitment(td)) == commitment_hex);
}

} // namespace

TEST_CASE("BCHN token_tests - manual vector: 2-byte immutable NFT; 253 fungible", "[bchn_non_vm][token]") {
    check_valid("7002" "cccc" "fdfd00", 253, "cccc");
}

TEST_CASE("BCHN token_tests - manual vector: 80-byte OUT OF CONSENSUS immutable NFT; 65536 fungible", "[bchn_non_vm][token]") {
    check_valid("7050" + std::string(160, 'c') + "fe00000100", 65536, std::string(160, 'c'));
}

TEST_CASE("BCHN token_tests - manual vector: commitment length without NFT is invalid", "[bchn_non_vm][token]") {
    auto const bytes = output_with_script("ef" + category_hex + "5001cc01" + spk_hex);
    byte_reader reader(bytes);
    CHECK( ! chain::output::from_data(reader, true));
}
