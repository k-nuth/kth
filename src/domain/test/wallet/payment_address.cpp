// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <kth/domain/multi_crypto_support.hpp>
#include <test_helpers.hpp>
#include <kth/domain/wallet/cashaddr.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::wallet;

// Public APIs must be usable from a consumer translation unit in C++23.
#if defined(KTH_CURRENCY_BCH)
namespace {
constexpr auto compile_time_address = payment_address::parse_from("bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl");
static_assert(compile_time_address.has_value());
static_assert(compile_time_address->version() == payment_address::mainnet_p2kh);
static_assert(compile_time_address->hash_span().size() == short_hash_size);

auto cashaddr_error(std::string_view text, domain::config::network net = domain::config::network::mainnet) {
    auto const parsed = payment_address::parse_from(text, net);
    CHECK_FALSE(parsed);
    return parsed ? kth::code{} : parsed.error();
}

constexpr std::string address_payload(uint8_t version, size_t size) {
    data_chunk raw(size + 1, 0x42);
    raw.front() = version;
    data_chunk payload;
    payment_address_detail::convert_bits<8, 5, true>(payload, raw.begin(), raw.end());
    return cashaddr::encode("bitcoincash", payload);
}

static_assert([] {
    auto const plain = payment_address::parse_from("bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl");
    auto const token = payment_address::parse_from("bitcoincash:zr7fzmep8g7h7ymfxy74lgc0v950j3r295z4y4gq0v");
    return plain && token && *plain == *token && plain->version() == payment_address::mainnet_p2kh
        && plain->to_string() == "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl"
        && token->encoded_token() == "bitcoincash:zr7fzmep8g7h7ymfxy74lgc0v950j3r295z4y4gq0v";
}());
static_assert([] {
    auto const upper = payment_address::parse_from("BITCOINCASH:QR7FZMEP8G7H7YMFXY74LGC0V950J3R2959LHTXXSL");
    auto const prefixless = payment_address::parse_from("qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl", domain::config::network::mainnet);
    return upper && prefixless && *upper == *prefixless;
}());
static_assert([] {
    for (auto net : {domain::config::network::mainnet, domain::config::network::testnet, domain::config::network::regtest}) {
        for (bool token : {false, true}) {
            hash_digest hash{};
            hash[0] = 0x42;
            auto const source = payment_address::from_hash(hash, net == domain::config::network::mainnet
                ? payment_address::mainnet_p2sh : payment_address::testnet_p2sh).value();
            auto const wire = source.encoded_cashaddr(token);
            auto const decoded = cashaddr::decode(wire, "");
            auto const input = cashaddr::encode(payment_address::cashaddr_prefix_for(net), decoded.second);
            auto const parsed = payment_address::parse_from(input, net);
            if (!parsed || *parsed != source || !parsed->encoded_legacy().empty() || parsed->to_payment() != payment{}) return false;
        }
    }
    return true;
}());
} // namespace

TEST_CASE("payment_address discriminates validation failures", "[payment_address]") {
    auto const address_error = [](std::string_view text) {
        auto const parsed = payment_address::parse_from(text);
        CHECK_FALSE(parsed);
        return parsed ? kth::code{} : parsed.error();
    };
    CHECK(address_error("") == kth::error::address_empty_input);
    CHECK(address_error("bogus") == kth::error::address_invalid_base58_length);
    CHECK(address_error("!") == kth::error::address_invalid_character);
    CHECK(address_error("unknown:qq") == kth::error::address_invalid_prefix);
    CHECK(cashaddr_error("bitcoincash::qq") == kth::error::address_invalid_prefix);
    CHECK(cashaddr_error("bitcoincash:!") == kth::error::address_invalid_character);
    CHECK(cashaddr_error("bitcoincash:Qq") == kth::error::address_mixed_case);
    CHECK(cashaddr_error("bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsq") == kth::error::address_invalid_checksum);
    CHECK(cashaddr_error("bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl", domain::config::network::testnet) == kth::error::address_network_mismatch);
    CHECK(cashaddr_error(address_payload(0x80, 20)) == kth::error::address_reserved_version);
    CHECK(cashaddr_error(address_payload(0x20, 20)) == kth::error::address_unsupported_type);
    CHECK(cashaddr_error(address_payload(0x00, 19)) == kth::error::address_invalid_hash_length);
    CHECK(cashaddr_error(address_payload(0x01, 24)) == kth::error::address_unsupported_hash_size);
    CHECK(cashaddr_error(address_payload(0x03, 32)) == kth::error::address_unsupported_hash_size);
    CHECK(cashaddr_error(address_payload(0x13, 32)) == kth::error::address_unsupported_hash_size);
    CHECK([] {
        auto payload = cashaddr::decode(address_payload(0, 20), "").second;
        payload.back() |= 1;
        return cashaddr_error(cashaddr::encode("bitcoincash", payload)) == kth::error::address_invalid_padding;
    }());
    CHECK(cashaddr_error(cashaddr::encode("bitcoincash", data_chunk{0})) == kth::error::address_invalid_padding);
    CHECK([] {
        auto const parsed = payment_address::parse_from(cashaddr::encode("bitcoincash", {}));
        return !parsed && parsed.error() == kth::error::address_invalid_hash_length;
    }());
}
#endif

TEST_CASE("payment_address errors retain their runtime category", "[payment_address]") {
    auto const invalid = payment_address::parse_from("");
    REQUIRE_FALSE(invalid);
    kth::code const error = invalid.error();
    CHECK(error.category().name() == std::string("bitcoin"));
    CHECK(error.message() == "empty address");
    payment bad_checksum{};
    auto const result = payment_address::from_payment(bad_checksum);
    REQUIRE_FALSE(result);
    CHECK(result.error() == kth::error::address_invalid_checksum);
    auto const invalid_script = payment_address::from_pay_public_key_hash_script(chain::script{}, payment_address::mainnet_p2kh);
    REQUIRE_FALSE(invalid_script);
    CHECK(invalid_script.error() == kth::error::address_invalid_script);
}

// Start Test Suite: payment_address tests

// Compile-time verification that both wrapping ctors and the
// hash / version accessors are usable in a constant expression. A
// regression that made any of the newly-constexpr pieces runtime-
// only would surface as a compile error here, closer to the type
// than a downstream call site.
namespace {

constexpr short_hash kShort = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14,
}};

constexpr hash_digest kFull = {{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
}};

constexpr auto kP2kh = payment_address::from_shorthash(kShort, payment_address::mainnet_p2kh).value();
static_assert(kP2kh.version() == payment_address::mainnet_p2kh);
static_assert(kP2kh.hash20() == kShort);
static_assert(kP2kh == kP2kh);

constexpr auto kFullHash = payment_address::from_hash(kFull, payment_address::mainnet_p2sh).value();
static_assert(kFullHash.hash32() == kFull);
// 32-byte hash carriers have no legal 20-byte representation — the
// accessor surfaces `null_short_hash` sentinel rather than truncating.
static_assert(kFullHash.hash20() == null_short_hash);

} // namespace

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

// negative tests:

TEST_CASE("payment_address construct string invalid invalid", "[payment_address]") {
    REQUIRE( ! payment_address::parse_from("bogus"));
}

// construct secret:

TEST_CASE("payment_address construct secret valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);
    payment_address const address = payment_address::from_ec_private(ec_private::from_verified_secret(*secret, ec_private::mainnet, true)).value();
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct secret testnet valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);

    // MSVC CTP loses the MSB (WIF prefix) byte of the literal version when
    // using this initializer, but the MSB isn't used by payment_address.
    payment_address const address = payment_address::from_ec_private(ec_private::from_verified_secret(*secret, 0x806f, true)).value();
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct secret mainnet uncompressed valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);
    payment_address const address = payment_address::from_ec_private(ec_private::from_verified_secret(*secret, payment_address::mainnet_p2kh, false)).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed);
}

TEST_CASE("payment_address construct secret testnet uncompressed valid expected", "[payment_address]") {
    auto const secret = decode_base16<ec_secret_size>(secret_hex);
    REQUIRE(secret);

    // MSVC CTP loses the MSB (WIF prefix) byte of the literal version when
    // using this initializer, but the MSB isn't used by payment_address.
    payment_address const address = payment_address::from_ec_private(ec_private::from_verified_secret(*secret, 0x806f, false)).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct public:

TEST_CASE("payment_address construct public valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct public testnet valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), 0x6f).value();
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct public uncompressed valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(uncompressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed);
}

TEST_CASE("payment_address construct public testnet uncompressed valid expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(uncompressed_pubkey).value(), 0x6f).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

TEST_CASE("payment_address construct public compressed from uncompressed testnet valid expected", "[payment_address]") {
    auto const point = decode_base16<ec_uncompressed_size>(uncompressed_pubkey);
    REQUIRE(point);
    auto const pub = ec_public::from_point(*point, true);
    REQUIRE(pub);
    payment_address const address = payment_address::from_ec_public(*pub, 0x6f).value();
    REQUIRE(address.encoded_legacy() == address_compressed_testnet);
}

TEST_CASE("payment_address construct public uncompressed from compressed testnet valid expected", "[payment_address]") {
    auto const point = decode_base16<ec_compressed_size>(compressed_pubkey);
    REQUIRE(point);
    payment_address const address = payment_address::from_ec_public(ec_public::from_verified_point(*point, false), 0x6f).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct hash:

TEST_CASE("payment_address construct hash valid expected", "[payment_address]") {
    auto const hash = decode_base16<short_hash_size>(compressed_hash);
    REQUIRE(hash);
    auto const address = payment_address::from_shorthash(*hash, payment_address::mainnet_p2kh).value();
    REQUIRE(address.encoded_legacy() == address_compressed);
}

TEST_CASE("payment_address construct uncompressed hash testnet valid expected", "[payment_address]") {
    auto const hash = decode_base16<short_hash_size>(uncompressed_hash);
    REQUIRE(hash);
    auto const address = payment_address::from_shorthash(*hash, 0x6f).value();
    REQUIRE(address.encoded_legacy() == address_uncompressed_testnet);
}

// construct script:

TEST_CASE("payment_address construct script valid expected", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, payment_address::mainnet_p2sh).value();
    REQUIRE(address.encoded_legacy() == address_script);
}

TEST_CASE("payment_address construct script testnet valid expected", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, 0xc4).value();
    REQUIRE(address.encoded_legacy() == address_script_testnet);
}

// construct payment:

TEST_CASE("payment_address construct payment valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_hex);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    REQUIRE(address.encoded_legacy() == address_script);
}

TEST_CASE("payment_address construct payment testnet valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_testnet);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    REQUIRE(address.encoded_legacy() == address_script_testnet);
}

// construct copy:

TEST_CASE("payment_address construct copy valid expected", "[payment_address]") {
    auto const pay = decode_base16<payment_size>(payment_hex);
    REQUIRE(pay);
    payment_address const address = payment_address::from_payment(*pay).value();
    payment_address const copy(address);
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
    REQUIRE(address.version() == testnet);
}

TEST_CASE("payment_address version script valid mainnet p2sh", "[payment_address]") {
    script ops;
    REQUIRE(ops.from_string(script_text));
    payment_address const address = payment_address::from_script(ops, payment_address::mainnet_p2sh).value();
    REQUIRE(address.version() == payment_address::mainnet_p2sh);
}

// hash property:

TEST_CASE("payment_address hash compressed point expected", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from(compressed_pubkey).value(), payment_address::mainnet_p2kh).value();
    REQUIRE(encode_base16(address.hash20()) == compressed_hash);
}

#if defined(KTH_CURRENCY_BCH)
//cashAddr payment_address
TEST_CASE("payment_address cashAddr mainnet encode", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from("04278f7bfee4ef625f85279c3a01d57c22e2877a902128b2df85071f9d6c95b290f094f5bd1bff5880d09cc231c774d71ac22d3ab9bdd9dda2e75017b52d893367").value(), kth::domain::wallet::payment_address::mainnet_p2kh).value();
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz");
}

TEST_CASE("payment_address cashAddr testnet encode", "[payment_address]") {
    payment_address const address = payment_address::from_ec_public(ec_public::parse_from("04278f7bfee4ef625f85279c3a01d57c22e2877a902128b2df85071f9d6c95b290f094f5bd1bff5880d09cc231c774d71ac22d3ab9bdd9dda2e75017b52d893367").value(), kth::domain::wallet::payment_address::testnet_p2kh).value();
    REQUIRE(address.encoded_cashaddr(false) == "bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707");
}

TEST_CASE("payment_address cashAddr mainnet from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz").value();
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz");
    REQUIRE(address.encoded_legacy() == "17DHrHvtmMRs9ciersFCPNhvJtryd5NWbT");
}

TEST_CASE("payment_address cashAddr testnet from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707", domain::config::network::testnet).value();
    REQUIRE(address.encoded_cashaddr(false) == "bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707");
    REQUIRE(address.encoded_legacy() == "mmjF9M1saNs7vjCGaSDaDHvFAtTgUNtfrJ");
}

TEST_CASE("payment_address token address from string", "[payment_address]") {
    auto const address = payment_address::parse_from("bitcoincash:pvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu69reyzmqx").value();
    REQUIRE(address.encoded_cashaddr(false) == "bitcoincash:pvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu69reyzmqx");
    REQUIRE(address.encoded_cashaddr(true) == "bitcoincash:rvstqkm54dtvnpyqxt5m5n7sjsn4enrlxc526xyxlnjkaycdzfeu6hs99m6ed");
    // 32-byte address (`pay_script_hash_32`) — no legacy
    // representation; `encoded_legacy()` returns an empty string
    // instead of truncating the hash to 20 bytes and producing a
    // plausible-looking but incorrect base58 result.
    REQUIRE(address.encoded_legacy().empty());
}

TEST_CASE("payment_address token-aware P2PKH cashAddr decodes as P2PKH", "[payment_address]") {
    // The pair from the CashTokens specification's address examples.
    auto const plain = payment_address::parse_from("bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl").value();
    auto const token = payment_address::parse_from("bitcoincash:zr7fzmep8g7h7ymfxy74lgc0v950j3r295z4y4gq0v").value();
    REQUIRE(token.version() == payment_address::mainnet_p2kh);
    REQUIRE(token == plain);
    REQUIRE(token.encoded_cashaddr(false) == "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl");
    REQUIRE(token.encoded_cashaddr(true) == "bitcoincash:zr7fzmep8g7h7ymfxy74lgc0v950j3r295z4y4gq0v");
}

TEST_CASE("payment_address token-aware cashAddr keeps the P2PKH or P2SH kind", "[payment_address]") {
    // One hash under each of the four types, on each network's prefix.
    auto const hash = decode_base16<short_hash_size>("4423cfd20e8f5f10863ae31df5634620d36b0fc3");
    REQUIRE(hash);

    struct address_case {
        char const* address;
        domain::config::network net;
        uint8_t version;
    };
    for (auto const& v : {
        address_case{"bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", domain::config::network::mainnet, payment_address::mainnet_p2kh},
        address_case{"bitcoincash:ppzz8n7jp6847yyx8t33matrgcsdx6c0cvvwyry2nl", domain::config::network::mainnet, payment_address::mainnet_p2sh},
        address_case{"bitcoincash:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvup2jd0h3", domain::config::network::mainnet, payment_address::mainnet_p2kh},
        address_case{"bitcoincash:rpzz8n7jp6847yyx8t33matrgcsdx6c0cvtyha2vvv", domain::config::network::mainnet, payment_address::mainnet_p2sh},
        address_case{"bchtest:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvcnw40csd", domain::config::network::testnet, payment_address::testnet_p2kh},
        address_case{"bchtest:rpzz8n7jp6847yyx8t33matrgcsdx6c0cv0kn6gmts", domain::config::network::testnet, payment_address::testnet_p2sh},
        address_case{"bchreg:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvz0c5vtnt", domain::config::network::regtest, payment_address::testnet_p2kh},
        address_case{"bchreg:rpzz8n7jp6847yyx8t33matrgcsdx6c0cv429mtggk", domain::config::network::regtest, payment_address::testnet_p2sh}}) {
        CAPTURE(v.address);
        auto const address = payment_address::parse_from(v.address, v.net);
        REQUIRE(address);
        REQUIRE(address->version() == v.version);
        REQUIRE(address->hash20() == *hash);
    }
}

TEST_CASE("payment_address cashAddr with an unknown type is rejected", "[payment_address]") {
    // Type 4, otherwise well formed, with a valid checksum.
    REQUIRE_FALSE(payment_address::parse_from("bitcoincash:ypzz8n7jp6847yyx8t33matrgcsdx6c0cv4lkel9ld"));
}

TEST_CASE("payment_address cashAddr with a hash of neither 20 nor 32 bytes is rejected", "[payment_address]") {
    // Well formed, with valid checksums: a 24-byte P2PKH and a 40-byte P2SH.
    REQUIRE_FALSE(payment_address::parse_from("bitcoincash:q9zz8n7jp6847yyx8t33matrgcsdx6c0cvqqqqqqs8e2yj0m"));
    REQUIRE_FALSE(payment_address::parse_from("bitcoincash:p3zz8n7jp6847yyx8t33matrgcsdx6c0cvqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqkuv60mtm"));
}

TEST_CASE("payment_address rejects 32-byte P2PKH cashAddr hashes", "[payment_address]") {
    struct address_case {
        char const* address;
        domain::config::network net;
    };
    for (auto const& v : {
        address_case{"bitcoincash:qvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqjyfd3c20", domain::config::network::mainnet},
        address_case{"bitcoincash:zvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqqh4vgeny", domain::config::network::mainnet},
        address_case{"bchtest:qvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jq3rcnftla", domain::config::network::testnet},
        address_case{"bchtest:zvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqrsyjs2xk", domain::config::network::testnet},
        address_case{"bchreg:qvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqyf2ewug4", domain::config::network::regtest},
        address_case{"bchreg:zvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqk6kcha37", domain::config::network::regtest}}) {
        CAPTURE(v.address);
        // The checksum is valid: rejection must come from the destination
        // type/size combination, rather than a malformed CashAddr string.
        REQUIRE_FALSE(cashaddr::decode(v.address, std::string(payment_address::cashaddr_prefix_for(v.net))).second.empty());
        auto const detected = payment_address::parse_from(v.address);
        auto const explicit_net = payment_address::parse_from(v.address, v.net);
        CHECK_FALSE(detected);
        CHECK_FALSE(explicit_net);
        if (!detected) CHECK(detected.error() == kth::error::address_unsupported_hash_size);
        if (!explicit_net) CHECK(explicit_net.error() == kth::error::address_unsupported_hash_size);
    }
}

TEST_CASE("payment_address accepts plain and token-aware P2SH32 cashAddr hashes", "[payment_address]") {
    auto const hash = decode_base16<hash_size>("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    REQUIRE(hash);
    struct address_case {
        char const* address;
        domain::config::network net;
    };
    for (auto const& v : {
        address_case{"bitcoincash:pvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqme8efvj7", domain::config::network::mainnet},
        address_case{"bitcoincash:rvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqf2mcsdt4", domain::config::network::mainnet},
        address_case{"bchtest:pvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqc7k83l8v", domain::config::network::testnet},
        address_case{"bchtest:rvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jq2d2xg778", domain::config::network::testnet},
        address_case{"bchreg:pvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqd5ydkgsy", domain::config::network::regtest},
        address_case{"bchreg:rvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jql8cv0ff0", domain::config::network::regtest}}) {
        CAPTURE(v.address);
        auto const detected = payment_address::parse_from(v.address);
        auto const explicit_net = payment_address::parse_from(v.address, v.net);
        REQUIRE(detected);
        REQUIRE(explicit_net);
        CHECK(*detected == *explicit_net);
        CHECK(detected->version() == (v.net == domain::config::network::mainnet
            ? payment_address::mainnet_p2sh : payment_address::testnet_p2sh));
        CHECK(detected->hash_span().size() == hash_size);
        CHECK(detected->hash32() == *hash);
    }
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
    auto const address = payment_address::from_hash(h32, payment_address::mainnet_p2sh).value();
    REQUIRE(address.hash20() == null_short_hash);
    REQUIRE(address.hash32() == h32);
}

TEST_CASE("payment_address 32-byte to_payment returns zero sentinel", "[payment_address]") {
    hash_digest h32;
    for (size_t i = 0; i < h32.size(); ++i) h32[i] = static_cast<uint8_t>(i + 1);
    auto const address = payment_address::from_hash(h32, payment_address::mainnet_p2sh).value();
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
    auto const address = payment_address::from_shorthash(*hash, payment_address::mainnet_p2kh).value();
    REQUIRE(address.hash20() == *hash);
    REQUIRE_FALSE(address.encoded_legacy().empty());
    REQUIRE(address.encoded_legacy() == address_compressed);
}

#endif

// End Test Suite

#if defined(KTH_CURRENCY_BCH)
static_assert([] {
    using type = payment_address_detail::cashaddr_destination_type;
    for (auto kind : {type::pubkey_hash, type::script_hash, type::token_pubkey_hash, type::token_script_hash}) {
        for (size_t size : {short_hash_size, hash_size}) {
            if (size == hash_size && kind != type::script_hash &&
                kind != type::token_script_hash) continue;
            hash_digest hash{};
            auto const packed = payment_address_detail::pack_addr_data_(byte_span{hash.data(), size}, kind);
            if (packed.size() != (size == short_hash_size ? 34 : 53)) return false;
        }
    }
    return true;
}());
TEST_CASE("payment_address rejects invalid components at construction", "[payment_address]") {
    auto const unknown = payment_address::from_shorthash(short_hash{}, 0xff);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error() == kth::error::address_unsupported_type);
    for (uint8_t version : {payment_address::mainnet_p2kh, payment_address::testnet_p2kh}) {
        auto const invalid = payment_address::from_hash(hash_digest{}, version);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error() == kth::error::address_unsupported_hash_size);
    }
    for (uint8_t version : {payment_address::mainnet_p2sh, payment_address::testnet_p2sh}) {
        auto const valid = payment_address::from_hash(hash_digest{}, version);
        REQUIRE(valid);
        CHECK_FALSE(valid->encoded_cashaddr(false).empty());
        CHECK_FALSE(valid->encoded_token().empty());
    }
    auto const invalid_script_version = payment_address::from_script(chain::script{}, 0xff);
    REQUIRE_FALSE(invalid_script_version);
    CHECK(invalid_script_version.error() == kth::error::address_unsupported_type);
}
#endif

TEST_CASE("common failed results preserve Knuth errors", "[payment_address]") {
    expect<int> const from_enum = kth::fail(kth::error::address_invalid_checksum);
    REQUIRE_FALSE(from_enum);
    CHECK(from_enum.error() == kth::error::address_invalid_checksum);
    expect<std::string> const propagated = kth::fail(from_enum.error());
    REQUIRE_FALSE(propagated);
    CHECK(propagated.error() == from_enum.error());
    expect<void> const empty = kth::fail(from_enum.error());
    REQUIRE_FALSE(empty);
    CHECK(empty.error() == from_enum.error());
}

TEST_CASE("payment_address factories reject unsupported versions before construction", "[payment_address]") {
    auto const public_key = ec_public::parse_from(compressed_pubkey);
    REQUIRE(public_key);
    auto const address = payment_address::from_ec_public(*public_key, 0xff);
    CHECK_FALSE(address);
    if (!address) CHECK(address.error() == kth::error::address_unsupported_type);
    auto const wire = wrap(0xff, short_hash{});
    auto const decoded = payment_address::from_payment(wire);
    CHECK_FALSE(decoded);
    if (!decoded) CHECK(decoded.error() == kth::error::address_unsupported_type);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("payment_address checked construction covers every version byte", "[payment_address]") {
    for (unsigned version = 0; version <= 255; ++version) {
        CAPTURE(version);
        bool const supported = payment_address::is_supported_version(version);
        bool const script = version == payment_address::mainnet_p2sh || version == payment_address::testnet_p2sh;
        auto const small = payment_address::from_shorthash(short_hash{}, version);
        auto const large = payment_address::from_hash(hash_digest{}, version);
        CHECK(small.has_value() == supported);
        CHECK(large.has_value() == script);
        if (!small) CHECK(small.error() == kth::error::address_unsupported_type);
        if (!large) CHECK(large.error() == (supported ? kth::error::address_unsupported_hash_size : kth::error::address_unsupported_type));
        for (auto const* address : {&small, &large}) {
            if (!*address) continue;
            for (bool token : {false, true}) {
                auto const encoded = (*address)->encoded_cashaddr(token);
                auto const parsed = payment_address::parse_from(encoded);
                REQUIRE(parsed);
                CHECK(*parsed == **address);
            }
        }
    }
}
#endif

static_assert(!std::is_constructible_v<payment_address, short_hash const&, uint8_t>);
static_assert(!std::is_constructible_v<payment_address, hash_digest const&, uint8_t>);
static_assert(std::is_copy_constructible_v<payment_address>);

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("CashAddr decoding uses the common Knuth errors", "[payment_address]") {
    struct invalid_case { char const* text; kth::error::error_code_t error; };
    for (auto const& test : {
        invalid_case{"bitcoincash::qq", kth::error::address_invalid_prefix},
        invalid_case{"bitcoincash:!", kth::error::address_invalid_character},
        invalid_case{"bitcoincash:Qq", kth::error::address_mixed_case},
        invalid_case{"bitcoincash:qq", kth::error::address_invalid_checksum}}) {
        expect<std::pair<std::string, data_chunk>> const decoded = cashaddr::decode_checked(test.text, "bitcoincash");
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == test.error);
    }
    auto const parsed = payment_address::parse_from("bitcoincash::qq");
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error() == cashaddr::decode_checked("bitcoincash::qq", "bitcoincash").error());
}
#endif

TEST_CASE("payment_address extraction drops only invalid destination versions", "[payment_address]") {
    chain::script script;
    REQUIRE(script.from_string(script_text));
    auto const valid = payment_address::extract_output(script, payment_address::mainnet_p2kh, payment_address::mainnet_p2sh);
    REQUIRE(valid.size() == 1);
    CHECK(valid.front().version() == payment_address::mainnet_p2kh);
    CHECK(payment_address::extract_output(script, 0xff, payment_address::mainnet_p2sh).empty());
    CHECK(payment_address::extract_output(script, payment_address::mainnet_p2kh, 0xff).size() == 1);
    for (auto const& p2sh : {
        chain::script{chain::script::to_pay_script_hash_pattern(short_hash{})},
        chain::script{chain::script::to_pay_script_hash_32_pattern(hash_digest{})}}) {
        CHECK(payment_address::extract_output(p2sh, 0xff, payment_address::mainnet_p2sh).size() == 1);
        CHECK(payment_address::extract_output(p2sh, payment_address::mainnet_p2kh, 0xff).empty());
    }
}

#if defined(KTH_CURRENCY_BCH)
static_assert([] {
    constexpr std::string_view framed = "xxBITCOINCASH:QR7FZMEP8G7H7YMFXY74LGC0V950J3R2959LHTXXSLyy";
    auto const view = framed.substr(2, framed.size() - 4);
    auto const address = payment_address::parse_from(view);
    auto const decoded = cashaddr::decode_checked(view, std::string_view{"bitcoincash"});
    return address && decoded && decoded->first == "bitcoincash" &&
        address->to_string() == "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl";
}());
TEST_CASE("payment_address parsing consumes bounded views without retaining input", "[payment_address]") {
    std::string framed = "xxbitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxslyy";
    auto const view = std::string_view{framed}.substr(2, framed.size() - 4);
    auto const address = payment_address::parse_from(view);
    auto const explicit_net = payment_address::parse_from(view, domain::config::network::mainnet);
    auto const decoded = cashaddr::decode_checked(view, "bitcoincash");
    REQUIRE(address);
    REQUIRE(explicit_net);
    REQUIRE(decoded);
    framed.assign(framed.size(), '!');
    CHECK(*address == *explicit_net);
    CHECK(address->to_string() == "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl");
    CHECK(decoded->first == "bitcoincash");
    CHECK(cashaddr::encode(decoded->first, decoded->second) == address->to_string());
}
#endif
