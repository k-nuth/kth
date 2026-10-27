// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>
#include <secp256k1_ellswift.h>

using namespace kth;

TEST_CASE("ElligatorSwift rejects secret keys outside the curve order", "[elliptic curve parity]") {
    unsigned char ell_a[64] = {};
    unsigned char ell_b[64] = {};
    unsigned char shared[32] = {};
    constexpr auto order = "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"_base16;
    auto key = order;
    auto exchange = [&] {
        return secp256k1_ellswift_xdh(secp256k1_context_static, shared,
            ell_a, ell_b, key.data(), 0,
            secp256k1_ellswift_xdh_hash_function_bip324, nullptr);
    };

    CHECK(exchange() == 0);
    ++key.back();
    CHECK(exchange() == 0);
    key = order;
    --key.back();
    CHECK(exchange() == 1);
    key.fill(0);
    CHECK(exchange() == 0);
    key.back() = 1;
    CHECK(exchange() == 1);
}

// Start Test Suite: elliptic curve tests

// Scenario 1
constexpr auto secret1 = "8010b1bb119ad37d4b65a1022a314897b1b3614b345974332cb1b9582cf03536"_base16;
constexpr auto compressed1 = "0309ba8621aefd3b6ba4ca6d11a4746e8df8d35d9b51b383338f627ba7fc732731";
constexpr auto uncompressed1 = "0409ba8621aefd3b6ba4ca6d11a4746e8df8d35d9b51b383338f627ba7fc7327318c3a6ec6acd33c36328b8fb4349b31671bcd3a192316ea4f6236ee1ae4a7d8c9";

// Scenario 2
constexpr auto compressed2 = "03bc88a1bd6ebac38e9a9ed58eda735352ad10650e235499b7318315cc26c9b55b"_base16;
static auto const sighash2 = "ed8f9b40c2d349c8a7e58cebe79faa25c21b6bb85b874901f72a1b3f1ad0a67f"_hash;
constexpr auto der_signature2 = "3045022100bc494fbd09a8e77d8266e2abdea9aef08b9e71b451c7d8de9f63cda33a62437802206b93edd6af7c659db42c579eb34a3a4cb60c28b5a6bc86fd5266d42f6b8bb67d";

// Scenario 3
static auto const secret3 = "ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333"_hash;
static auto const sighash3 = "f89572635651b2e4f89778350616989183c98d1a721c911324bf9f17a0cf5bf0"_hash;
constexpr auto ec_signature3 = "57d0b4fb0df5cefa245e76ba9b099bb63aa46340cb152a927c1cb3f8befe32483750eae5727db3e6cc4275062971552e467f7cfd7269fec2626588b6c6d58e92"_base16;
constexpr auto der_signature3 = "3044022057d0b4fb0df5cefa245e76ba9b099bb63aa46340cb152a927c1cb3f8befe324802203750eae5727db3e6cc4275062971552e467f7cfd7269fec2626588b6c6d58e92";

TEST_CASE("elliptic curve secret to public positive test", "[elliptic curve tests]") {
    ec_compressed point;
    REQUIRE(secret_to_public(point, secret1));
    REQUIRE(encode_base16(point) == compressed1);
}

TEST_CASE("elliptic curve decompress positive test", "[elliptic curve tests]") {
    ec_uncompressed uncompressed;
    auto const comp = decode_base16<ec_compressed_size>(compressed1);
    REQUIRE(comp);
    REQUIRE(decompress(uncompressed, *comp));
    REQUIRE(encode_base16(uncompressed) == uncompressed1);
}

TEST_CASE("elliptic curve sign positive test", "[elliptic curve tests]") {
    ec_signature signature;
    REQUIRE(sign_ecdsa(signature, secret3, sighash3));
    REQUIRE(signature == ec_signature3);
}

TEST_CASE("elliptic curve encode signature positive test", "[elliptic curve tests]") {
    der_signature out;
    REQUIRE(encode_signature(out, ec_signature3));

    auto const result = encode_base16(out);
    REQUIRE(result == der_signature3);
}

TEST_CASE("elliptic curve sign round trip positive test", "[elliptic curve tests]") {
    ec_compressed point;
    ec_signature signature;
    data_chunk const data{ 'd', 'a', 't', 'a' };
    hash_digest const hash = bitcoin_hash(data);
    REQUIRE(secret_to_public(point, secret1));
    REQUIRE(sign_ecdsa(signature, secret1, hash));
    REQUIRE(verify_signature(point, hash, signature));
}

TEST_CASE("elliptic curve sign round trip negative test", "[elliptic curve tests]") {
    ec_compressed point;
    ec_signature signature;
    data_chunk const data{ 'd', 'a', 't', 'a' };
    hash_digest hash = bitcoin_hash(data);
    REQUIRE(secret_to_public(point, secret1));
    REQUIRE(sign_ecdsa(signature, secret1, hash));

    // Invalidate the positive test.
    hash[0] = 0;
    REQUIRE( ! verify_signature(point, hash, signature));
}

TEST_CASE("elliptic curve verify signature positive test", "[elliptic curve tests]") {
    ec_signature signature;
    static auto const strict = false;
    hash_digest const sighash = sighash2;
    auto const distinguished = decode_base16(der_signature2);
    REQUIRE(distinguished);
    REQUIRE(parse_signature(signature, *distinguished, strict));
    REQUIRE(verify_signature(compressed2, sighash, signature));
}

TEST_CASE("elliptic curve verify signature negative test", "[elliptic curve tests]") {
    ec_signature signature;
    static auto const strict = false;
    hash_digest const sighash = sighash2;
    auto const distinguished = decode_base16(der_signature2);
    REQUIRE(distinguished);
    REQUIRE(parse_signature(signature, *distinguished, strict));

    // Invalidate the positive test.
    signature[10] = 110;
    REQUIRE( ! verify_signature(compressed2, sighash, signature));
}

TEST_CASE("elliptic curve ec add positive test", "[elliptic curve tests]") {
    ec_secret secret1{ { 1, 2, 3 } };
    ec_secret const secret2{ { 3, 2, 1 } };
    ec_compressed public1;
    REQUIRE(secret_to_public(public1, secret1));
    REQUIRE(ec_add(secret1, secret2));
    REQUIRE(encode_base16(secret1) == "0404040000000000000000000000000000000000000000000000000000000000");

    ec_compressed public2;
    REQUIRE(secret_to_public(public2, secret1));
    REQUIRE(ec_add(public1, secret2));
    REQUIRE(std::equal(public1.begin(), public1.end(), public2.begin()));
}

TEST_CASE("elliptic curve ec add negative test", "[elliptic curve tests]") {
    // = n - 1
    ec_secret secret1 = "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140"_base16;
    ec_secret secret2{ { 0 } };
    secret2[31] = 1;
    ec_compressed public1;
    REQUIRE(secret_to_public(public1, secret1));
    REQUIRE( ! ec_add(secret1, secret2));
    REQUIRE( ! ec_add(public1, secret2));
}

TEST_CASE("elliptic curve ec multiply test", "[elliptic curve tests]") {
    ec_secret secret1{{0}};
    ec_secret secret2{{0}};
    secret1[31] = 11;
    secret2[31] = 22;
    ec_compressed public1;
    REQUIRE(secret_to_public(public1, secret1));
    REQUIRE(ec_multiply(secret1, secret2));
    REQUIRE(secret1[31] == 242u);
    REQUIRE(ec_multiply(public1, secret2));

    ec_compressed public2;
    REQUIRE(secret_to_public(public2, secret1));
    REQUIRE(std::equal(public1.begin(), public1.end(), public2.begin()));
}


// Parity controls for the secp256k1 library: known answers that must hold for
// any conforming implementation of these schemes, independent of the library
// version (deterministic ECDSA nonce derivation is deliberately not covered).

// BCH Schnorr (not BIP340): RFC6979 nonce with "Schnorr+SHA256  " as algorithm
// data, R.y forced to a quadratic residue, e = SHA256(R.x || P || m).
// Vectors 1 and 4 take the nonce negation branch (R.y not a residue).
struct schnorr_known_answer {
    char const* secret;
    char const* hash;
    char const* point;
    char const* signature;
};

static schnorr_known_answer const schnorr_known_answers[] = {
    {"8010b1bb119ad37d4b65a1022a314897b1b3614b345974332cb1b9582cf03536",
     "ed8f9b40c2d349c8a7e58cebe79faa25c21b6bb85b874901f72a1b3f1ad0a67f",
     "0309ba8621aefd3b6ba4ca6d11a4746e8df8d35d9b51b383338f627ba7fc732731",
     "cca6565f8d6155d21cdad96cf7d124ca6a95fbc1a41a484c411ba6296c36fd467df5c8375abc986c8ca81c4a7fd6bc40b8a01156479c15942759be539447a9f5"},
    {"0000000000000000000000000000000000000000000000000000000000000001",
     "0000000000000000000000000000000000000000000000000000000000000000",
     "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
     "31ab79a6591ae4af37c4223bfa75b7396ac9c14950ea78d94afc14be3a6b61dc2a901f43c609dfb10dfe3f00015957f6461d48b61df9f452143ac6f6f7dd4edd"},
    {"3333333333333333333333333333333333333333333333333333333333333333",
     "5e2d58d8b3bcdf1abadec7829054f90dda9805aab56c77333024b9d0a508b75c",
     "023c72addb4fdf09af94f0c94d7fe92a386a7e70cf8a1d85916386bb2535c7b1b1",
     "78f00ab2e5a52932e9bd486079d055ac76061f07c586069f76fbce9050931e41a48a0a6c08b5982dda81759830122c30e4fa2aa33d92ba2fe5ef352f31e19c24"},
    {"fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140",
     "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89",
     "0379be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
     "b1494e8606523912b7b0d77f211cc89978b8f7d4a539ee99dc1e5c41e67c92efbab3997d3257b2a50afa9d58ef5426b31efb418d9219964d30008f1164c8ad47"},
};

TEST_CASE("elliptic curve schnorr sign known answers", "[elliptic curve parity]") {
    for (auto const& vector : schnorr_known_answers) {
        auto const secret = decode_base16<ec_secret_size>(vector.secret);
        auto const hash = decode_base16<hash_size>(vector.hash);
        REQUIRE(secret);
        REQUIRE(hash);

        ec_compressed point;
        REQUIRE(secret_to_public(point, *secret));
        REQUIRE(encode_base16(point) == vector.point);

        ec_signature signature;
        REQUIRE(sign_schnorr(signature, *secret, *hash));
        REQUIRE(encode_base16(signature) == vector.signature);
        REQUIRE(verify_schnorr(point, *hash, signature));
    }
}

TEST_CASE("elliptic curve schnorr verify known answers", "[elliptic curve parity]") {
    for (auto const& vector : schnorr_known_answers) {
        auto const point = decode_base16(vector.point);
        auto const hash = decode_base16<hash_size>(vector.hash);
        auto const signature = decode_base16(vector.signature);
        REQUIRE(point);
        REQUIRE(hash);
        REQUIRE(signature);
        REQUIRE(verify_schnorr(*point, *hash, *signature));

        // Any change to R.x, s or the message invalidates the signature.
        for (auto const position : {size_t{0}, size_t{31}, size_t{32}, size_t{63}}) {
            auto mutated = *signature;
            mutated[position] ^= 0x01;
            REQUIRE( ! verify_schnorr(*point, *hash, mutated));
        }
        auto other = *hash;
        other[0] ^= 0x01;
        REQUIRE( ! verify_schnorr(*point, other, *signature));
    }
}

TEST_CASE("elliptic curve schnorr verify rejects out of range values", "[elliptic curve parity]") {
    auto const& vector = schnorr_known_answers[1];
    auto const point = decode_base16(vector.point);
    auto const hash = decode_base16<hash_size>(vector.hash);
    auto const signature = decode_base16(vector.signature);
    REQUIRE(point);
    REQUIRE(hash);
    REQUIRE(signature);

    // R.x = p (field size) must be rejected, not reduced.
    auto const field_size = decode_base16("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    REQUIRE(field_size);
    auto r_overflow = *signature;
    std::copy(field_size->begin(), field_size->end(), r_overflow.begin());
    REQUIRE( ! verify_schnorr(*point, *hash, r_overflow));

    // s = n (group order) must be rejected, not reduced.
    auto const group_order = decode_base16("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    REQUIRE(group_order);
    auto s_overflow = *signature;
    std::copy(group_order->begin(), group_order->end(), s_overflow.begin() + 32);
    REQUIRE( ! verify_schnorr(*point, *hash, s_overflow));

    // Wrong sizes are rejected before parsing.
    data_chunk const short_signature(signature->begin(), signature->end() - 1);
    REQUIRE( ! verify_schnorr(*point, *hash, short_signature));
}

// ECDSA: a signature produced by the previous library (its nonce derivation
// differs) and the current one both verify, and DER round trips exactly.
constexpr auto compressed3 = "0252621ccce4d878a1ff6fa5264a5ce0127ddccad96497c2a125e28838306b4f94"_base16;
constexpr auto der_signature3_previous = "3044022063e65fb69754fd32e80c860964d929438b602f28fa6933d42bede4485cb2b717022031613536d9b9384f88d64ccc397188b123edc6c1c801448c869152ff4de301b9";
constexpr auto der_signature3_current = "3044022057d0b4fb0df5cefa245e76ba9b099bb63aa46340cb152a927c1cb3f8befe324802203750eae5727db3e6cc4275062971552e467f7cfd7269fec2626588b6c6d58e92";

// der_signature2 with an unnecessary leading zero in R: valid only when lax.
constexpr auto der_signature2_lax = "304602220000bc494fbd09a8e77d8266e2abdea9aef08b9e71b451c7d8de9f63cda33a62437802206b93edd6af7c659db42c579eb34a3a4cb60c28b5a6bc86fd5266d42f6b8bb67d";

TEST_CASE("elliptic curve ecdsa der verify parity", "[elliptic curve parity]") {
    ec_compressed point;
    REQUIRE(secret_to_public(point, secret3));
    REQUIRE(point == compressed3);

    for (auto const* der : {der_signature3_previous, der_signature3_current}) {
        auto const distinguished = decode_base16(der);
        REQUIRE(distinguished);

        for (auto const strict : {true, false}) {
            ec_signature signature;
            REQUIRE(parse_signature(signature, *distinguished, strict));
            REQUIRE(verify_signature(point, sighash3, signature));

            der_signature encoded;
            REQUIRE(encode_signature(encoded, signature));
            REQUIRE(encode_base16(encoded) == der);

            auto other = sighash3;
            other[0] ^= 0x01;
            REQUIRE( ! verify_signature(point, other, signature));
        }
    }
}

TEST_CASE("elliptic curve ecdsa lax der parity", "[elliptic curve parity]") {
    auto const distinguished = decode_base16(der_signature2_lax);
    REQUIRE(distinguished);

    ec_signature signature;
    REQUIRE( ! parse_signature(signature, *distinguished, true));
    REQUIRE(parse_signature(signature, *distinguished, false));
    REQUIRE(verify_signature(compressed2, sighash2, signature));

    der_signature encoded;
    REQUIRE(encode_signature(encoded, signature));
    REQUIRE(encode_base16(encoded) == der_signature2);
}

TEST_CASE("elliptic curve ecdsa high s verifies after normalization", "[elliptic curve parity]") {
    // s' = n - s of der_signature3_current: same signature, high-S form.
    constexpr auto der_high_s = "3045022057d0b4fb0df5cefa245e76ba9b099bb63aa46340cb152a927c1cb3f8befe3248022100c8af151a8d824c1933bd8af9d68eaad0742f5fe93cdea1795d6cd5d60960b2af";
    auto const distinguished = decode_base16(der_high_s);
    REQUIRE(distinguished);
    REQUIRE( ! check_low_s(*distinguished));

    ec_signature signature;
    REQUIRE(parse_signature(signature, *distinguished, true));
    REQUIRE(verify_signature(compressed3, sighash3, signature));
}
