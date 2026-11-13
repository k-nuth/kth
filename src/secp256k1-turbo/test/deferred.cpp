// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <thread>

#include <secp256k1.h>
#include <secp256k1_schnorr.h>

#include <secp256k1_turbo/deferred.hpp>

#include "helpers.hpp"

using namespace secp256k1_turbo;
using namespace secp256k1_turbo::test;

namespace {

struct sample {
    std::array<std::uint8_t, 64> sig;
    bytes32 msg;
    std::array<std::uint8_t, 33> pubkey;
};

sample make(rng& r, bool schnorr) {
    static secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    sample s;
    bytes32 seckey;
    do { seckey = r.b32(); } while (secp256k1_ec_seckey_verify(ctx, seckey.data()) != 1);
    s.msg = r.b32();
    secp256k1_pubkey pk;
    REQUIRE(secp256k1_ec_pubkey_create(ctx, &pk, seckey.data()) == 1);
    std::size_t len = 33;
    secp256k1_ec_pubkey_serialize(ctx, s.pubkey.data(), &len, &pk, SECP256K1_EC_COMPRESSED);
    if (schnorr) {
        REQUIRE(secp256k1_schnorr_sign(ctx, s.sig.data(), s.msg.data(), seckey.data(), nullptr, nullptr) == 1);
    } else {
        secp256k1_ecdsa_signature sig;
        REQUIRE(secp256k1_ecdsa_sign(ctx, &sig, s.msg.data(), seckey.data(), nullptr, nullptr) == 1);
        secp256k1_ecdsa_signature_serialize_compact(ctx, s.sig.data(), &sig);
    }
    return s;
}

} // namespace

TEST_CASE("deferred: allowed only inside a scope of an active collection", "[deferred]") {
    CHECK_FALSE(deferred::active());
    CHECK_FALSE(deferred::allowed());
    {
        deferred::allow_scope s(true);
        CHECK_FALSE(deferred::allowed());  // not active
    }
    deferred::begin();
    CHECK(deferred::active());
    CHECK_FALSE(deferred::allowed());
    {
        deferred::allow_scope s(true);
        CHECK(deferred::allowed());
        {
            deferred::allow_scope inner(false);
            CHECK_FALSE(deferred::allowed());
        }
        CHECK(deferred::allowed());
    }
    CHECK_FALSE(deferred::allowed());
    auto const r = deferred::end();
    CHECK(r.total == 0);
    CHECK(r.invalid == 0);
    CHECK_FALSE(deferred::active());
}

TEST_CASE("deferred: end verifies everything recorded and counts failures", "[deferred]") {
    rng r(0x5eed0040);
    deferred::begin();
    std::size_t expected_invalid = 0;
    for (int i = 0; i < 40; ++i) {
        bool const schnorr = (i % 2) == 1;
        sample s = make(r, schnorr);
        bool const corrupt_sig = (i % 5 == 0);
        bool const bad_key = (i % 11 == 0);  // recorded with a bad key size
        if (corrupt_sig) s.sig[7] ^= 0x10;
        if (corrupt_sig || bad_key) ++expected_invalid;
        std::size_t const key_size = bad_key ? 20 : 33;
        if (schnorr) deferred::record_schnorr(s.sig.data(), s.msg.data(), s.pubkey.data(), key_size);
        else deferred::record_ecdsa(s.sig.data(), s.msg.data(), s.pubkey.data(), key_size);
    }
    CHECK(deferred::pending() == 40);
    auto const res = deferred::end();
    CHECK(res.total == 40);
    CHECK(res.invalid == expected_invalid);
    CHECK(deferred::pending() == 0);
    // begin() always starts fresh
    deferred::begin();
    CHECK(deferred::pending() == 0);
    CHECK(deferred::end().total == 0);
}

TEST_CASE("deferred: state is per thread", "[deferred]") {
    deferred::begin();
    bool other_active = true;
    std::thread t([&] { other_active = deferred::active(); });
    t.join();
    CHECK_FALSE(other_active);
    CHECK(deferred::active());
    deferred::end();
}
