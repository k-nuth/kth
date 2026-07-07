// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <kth/domain/multi_crypto_support.hpp>
#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::wallet;

// Start Test Suite: payment_address tests

// $ bx base16-encode "Satoshi" | bx sha256
constexpr char secret_hex[] = "002688cc350a5333a87fa622eacec626c3d1c0ebf9f3793de3885fa254d7e393";
constexpr char script_text[] = "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig";

// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public
constexpr char compressed_pubkey[] = "03d24123978d696a6c964f2dcb1d1e000d4150102fbbcc37f020401e35fb4cb745";
// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public -u
constexpr char uncompressed_pubkey[] = "04d24123978d696a6c964f2dcb1d1e000d4150102fbbcc37f020401e35fb4cb74561a3362716303b0469f04c3d0e3cbc4b5b62a2da7add6ecc3b254404b12d2f83";

// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public | bx bitcoin160
constexpr char compressed_hash[] = "f85beb6356d0813ddb0dbb14230a249fe931a135";
// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public -u | bx bitcoin160
constexpr char uncompressed_hash[] = "96ec4e06c665b7bd62cbe3d232f7c2d34016e136";

// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public | bx ec-to-address
constexpr char address_compressed[] = "1PeChFbhxDD9NLbU21DfD55aQBC4ZTR3tE";
// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public -u | bx ec-to-address
constexpr char address_uncompressed[] = "1Em1SX7qQq1pTmByqLRafhL1ypx2V786tP";

// $ bx base16-encode "Satoshi" | bx sha256 | bx ec-to-public | bx ec-to-address -v 111
constexpr char address_compressed_testnet[] = "n4A9zJggmEeQ9T55jaC32zHuGAnmSzPU2L";
// $ bx script-encode "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig"
constexpr char address_uncompressed_testnet[] = "muGxjaCpDrT5EsfbYuPxVcYLqpYjNQnbkR";

// $ bx script-to-address "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig"
constexpr char address_script[] = "3CPSWnCGjkePffNyVptkv45Bx35SaAwm7d";
// $ bx script-to-address "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig" -v 196
constexpr char address_script_testnet[] = "2N3weaX8JMD9jsT1XAxWdY14TAPHcKYKHCT";

// $ bx script-to-address "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig" | bx base58-decode
constexpr char payment_hex[] = "0575566c599452b7bcb7f8cd4087bde9686fa9c52d8c2a7d90";
// $ bx script-to-address "dup hash160 [18c0bd8d1818f1bf99cb1df2269c645318ef7b73] equalverify checksig" -v 196 | bx base58-decode
constexpr char payment_testnet[] = "c475566c599452b7bcb7f8cd4087bde9686fa9c52d2fba2898";

// $ bx base58-decode 1111111111111111111114oLvT2 | bx wrap-decode
// wrapper
// {
//     checksum 285843604
//     payload 0000000000000000000000000000000000000000
//     version 0
// }
constexpr char uninitialized_address[] = "1111111111111111111114oLvT2";

// negative tests:

TEST_CASE("payment_address construct default invalid", "[payment_address]") {
    payment_address const address;
    REQUIRE( ! address.valid());
    REQUIRE(address.encoded_legacy() == uninitialized_address);
}

TEST_CASE("payment_address construct string invalid invalid", "[payment_address]") {
    REQUIRE( ! payment_address::parse_from("bogus"));
}

// construct secret:

TEST_CASE("payment_address construct secret valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);
    payment_address const address = payment_address::from_ec_private(ec_private{*secret}).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct secret testnet valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);

    // MSVC CTP loses the MSB (WIF prefix) byte of the literal version when
    // using this initializer, but the MSB isn't used by payment_address.
    payment_address const address = payment_address::from_ec_private(ec_private{*secret, 0x806f}).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct secret mainnet uncompressed valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);
    payment_address const address = payment_address::from_ec_private(ec_private{*secret, payment_address::mainnet_p2kh, false}).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed);
}

TEST_CASE("payment_address construct secret testnet uncompressed valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);

    // MSVC CTP loses the MSB (WIF prefix) byte of the literal version when
    // using this initializer, but the MSB isn't used by payment_address.
    payment_address const address = payment_address::from_ec_private(ec_private{*secret, 0x806f, false}).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct public:

TEST_CASE("payment_address construct public valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct public testnet valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), 0x6f).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct public uncompressed valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(uncompressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed);
}

TEST_CASE("payment_address construct public testnet uncompressed valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(uncompressed_pubkey).value(), 0x6f).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

TEST_CASE("payment_address construct public compressed from uncompressed testnet valid expected", "[payment_address]") {
    auto const point = decode_base16<ec_uncompressed_size>(uncompressed_pubkey);
    REQUIRE(point);
    auto const pub = ec_public::from_point(*point, true);
    REQUIRE(pub);
    payment_address const address = payment_address::from_ec_public(*pub, 0x6f).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct public uncompressed from compressed testnet valid expected", "[payment_address]") {
    auto const point = decode_base16<ec_compressed_size>(compressed_pubkey);
    REQUIRE(point);
    payment_address const address = payment_address::from_ec_public(ec_public{*point, false}, 0x6f).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct hash:

TEST_CASE("payment_address construct hash valid expected", "[payment_address]") {
    auto const hash = decode_base16<short_hash_size>(compressed_hash);
    REQUIRE(hash);
    payment_address const address(*hash, payment_address::mainnet_p2kh);
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct uncompressed hash testnet valid expected", "[payment_address]") {
    auto const hash = decode_base16<short_hash_size>(uncompressed_hash);
    REQUIRE(hash);
    payment_address const address(*hash, 0x6f);
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct script:

TEST_CASE("payment_address construct script valid expected", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, payment_address::mainnet_p2sh);
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_script);
}

TEST_CASE("payment_address construct script testnet valid expected", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, 0xc4);
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_script_testnet);
}

// construct payment:

TEST_CASE("payment_address construct payment valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_hex);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_script);
}

TEST_CASE("payment_address construct payment testnet valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_testnet);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_legacy() == address_script_testnet);
}

// construct copy:

TEST_CASE("payment_address construct copy valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_hex);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    payment_address const copy(address);
    REQUIRE(copy.valid());
    REQUIRE(copy.encoded_legacy() == address_script);
}

// version property:

TEST_CASE("payment_address version default mainnet", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.version() == payment_address::mainnet_p2kh);
}

TEST_CASE("payment_address version testnet testnet", "[payment_address]") {
    uint8_t const testnet = 0x6f;
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), testnet).value();
    REQUIRE(address.valid());
    REQUIRE(address.version() == testnet);
}

TEST_CASE("payment_address version script valid mainnet p2sh", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, payment_address::mainnet_p2sh);
    REQUIRE(address.valid());
    REQUIRE(address.version() == payment_address::mainnet_p2sh);
}

// hash property:

TEST_CASE("payment_address hash compressed point expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.valid());
    REQUIRE(encode_base16(address.hash20()) == compressed_hash);
}

#if defined(KTH_CURRENCY_BCH)
//cashAddr payment_address
TEST_CASE("payment_address cashAddr mainnet encode", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from("04278f7bfee4ef625f85279c3a01d57c22e2877a902128b2df85071f9d6c95b290f094f5bd1bff5880d09cc231c774d71ac22d3ab9bdd9dda2e75017b52d893367").value(), kth::domain::wallet::payment_address::mainnet_p2kh).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz");
}

TEST_CASE("payment_address cashAddr testnet encode", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from("04278f7bfee4ef625f85279c3a01d57c22e2877a902128b2df85071f9d6c95b290f094f5bd1bff5880d09cc231c774d71ac22d3ab9bdd9dda2e75017b52d893367").value(), kth::domain::wallet::payment_address::testnet_p2kh).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_cashaddr(false) == "bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707");
}

TEST_CASE("payment_address cashAddr mainnet from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz").value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz");
    REQUIRE(address.encoded_legacy() == "17DHrHvtmMRs9ciersFCPNhvJtryd5NWbT");
}

TEST_CASE("payment_address cashAddr testnet from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707", domain::config::network::testnet).value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_cashaddr(false) == "bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707");
    REQUIRE(address.encoded_legacy() == "mmjF9M1saNs7vjCGaSDaDHvFAtTgUNtfrJ");
}

TEST_CASE("payment_address token address from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bitcoincash:pvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu69reyzmqx").value();
    REQUIRE(address.valid());
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:pvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu69reyzmqx");
    REQUIRE(address.encoded_cashaddr(true) == "bitcoincash:rvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu6hs99m6ed");
    // 32-byte address (`pay_script_hash_32`) — no legacy
    // representation; `encoded_legacy()` returns an empty string
    // instead of truncating the hash to 20 bytes and producing a
    // plausible-looking but incorrect base58 result.
    REQUIRE(address.encoded_legacy().empty());
}

// ---------------------------------------------------------------------------
// 32-byte hash truncation guards (`pay_script_hash_32`, BCH 2025 Leibniz)
// ---------------------------------------------------------------------------

TEST_CASE("payment_address 32-byte hash20 returns null sentinel", "[payment_address]") {
    // Build a `payment_address` directly from a 32-byte hash, then
    // call the 20-byte accessor. Previous behaviour was a silent
    // copy of the first 20 bytes (truncation); the fix surfaces
    // that as the all-zeros sentinel so callers can detect it.
    hash_digest h32;
    for (size_t i = 0; i < h32.size(); ++i) h32[i] = static_cast<uint8_t>(i + 1);
    payment_address const address(h32, payment_address::mainnet_p2sh);
    REQUIRE(address.valid());
    REQUIRE(address.hash20() == null_short_hash);
    REQUIRE(address.hash32() == h32);
}

TEST_CASE("payment_address 32-byte to_payment returns zero sentinel", "[payment_address]") {
    hash_digest h32;
    for (size_t i = 0; i < h32.size(); ++i) h32[i] = static_cast<uint8_t>(i + 1);
    payment_address const address(h32, payment_address::mainnet_p2sh);
    REQUIRE(address.valid());
    // `payment` is `byte_array<25>`; zero-initialised compares
    // equal against any other zero-initialised `payment`.
    payment const zero_payment{};
    REQUIRE(address.to_payment() == zero_payment);
}

TEST_CASE("payment_address 20-byte accessors stay untouched on 20-byte hashes", "[payment_address]") {
    // Regression: the 32-byte guard must not engage for the
    // common P2KH / P2SH case.
    auto const hash = decode_base16<short_hash_size>(compressed_hash);
    REQUIRE(hash);
    payment_address const address(*hash, payment_address::mainnet_p2kh);
    REQUIRE(address.valid());
    REQUIRE(address.hash20() == *hash);
    REQUIRE_FALSE(address.encoded_legacy().empty());
    REQUIRE(address.encoded_legacy() == address_compressed);
}

#endif

// End Test Suite
