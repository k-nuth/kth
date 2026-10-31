// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>
#include <stddef.h>
#include <string.h>
#include <kth/capi/error.h>
#include <kth/capi/wallet/hd_private.h>
#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/vm/number.h>
#include <kth/capi/vm/big_number.h>

TEST_CASE("C-API HD paths match indexed derivation and preserve failed output slots",
          "[C-API WASM surface][HD]") {
    uint8_t const seed[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint64_t const prefixes = 0x0488ADE40488B21Eull;
    kth_hd_private_mut_t master = NULL;
    REQUIRE(kth_wallet_hd_private_from_seed(seed, sizeof(seed), prefixes, &master) == kth_ec_success);
    kth_hd_private_mut_t hardened = NULL;
    REQUIRE(kth_wallet_hd_private_derive_private(master, 0x80000000u, &hardened) == kth_ec_success);
    kth_hd_private_mut_t indexed = NULL;
    REQUIRE(kth_wallet_hd_private_derive_private(hardened, 1, &indexed) == kth_ec_success);
    kth_hd_private_mut_t path = NULL;
    REQUIRE(kth_wallet_hd_private_derive_path(master, "m/0'/1", &path) == kth_ec_success);
    REQUIRE(kth_wallet_hd_private_equals(path, indexed));
    kth_hd_private_mut_t seeded = NULL;
    REQUIRE(kth_wallet_hd_private_from_seed_and_path(seed, sizeof(seed), "m/0'/1", prefixes, &seeded) == kth_ec_success);
    REQUIRE(kth_wallet_hd_private_equals(seeded, indexed));

    kth_hd_public_mut_t pub = kth_wallet_hd_private_to_public(master);
    REQUIRE(pub != NULL);
    kth_hd_public_mut_t public_indexed = NULL;
    REQUIRE(kth_wallet_hd_public_derive_public(pub, 1, &public_indexed) == kth_ec_success);
    kth_hd_public_mut_t public_path = NULL;
    REQUIRE(kth_wallet_hd_public_derive_path(pub, "m/1", &public_path) == kth_ec_success);
    REQUIRE(kth_wallet_hd_public_equals(public_path, public_indexed));

    kth_hd_private_mut_t failed_private = NULL;
    REQUIRE(kth_wallet_hd_private_derive_path(master, "m//1", &failed_private) != kth_ec_success);
    REQUIRE(failed_private == NULL);
    kth_hd_public_mut_t failed_public = NULL;
    REQUIRE(kth_wallet_hd_public_derive_path(pub, "m/0'", &failed_public) != kth_ec_success);
    REQUIRE(failed_public == NULL);
    kth_hd_private_mut_t failed_seed = NULL;
    REQUIRE(kth_wallet_hd_private_from_seed_and_path(seed, sizeof(seed), "invalid", prefixes, &failed_seed) != kth_ec_success);
    REQUIRE(failed_seed == NULL);

    kth_hash_t secret = kth_wallet_hd_private_secret(master);
    // Keep the packed private/public prefixes; to_public() drops the private prefix.
    kth_hd_public_mut_t base = kth_wallet_hd_public_copy(kth_wallet_hd_private_public_key(master));
    REQUIRE(base != NULL);
    kth_hd_private_mut_t reconstructed = kth_wallet_hd_private_from_verified_parts(base, &secret);
    REQUIRE(reconstructed != NULL);
    kth_wallet_hd_public_destruct(base);
    memset(&secret, 0, sizeof(secret));
    kth_wallet_hd_public_destruct(pub);
    REQUIRE(kth_wallet_hd_private_equals(reconstructed, master));
    kth_wallet_hd_private_destruct(reconstructed);
    kth_wallet_hd_public_destruct(public_path);
    kth_wallet_hd_public_destruct(public_indexed);
    kth_wallet_hd_private_destruct(seeded);
    kth_wallet_hd_private_destruct(path);
    kth_wallet_hd_private_destruct(indexed);
    kth_wallet_hd_private_destruct(hardened);
    kth_wallet_hd_private_destruct(master);
}

TEST_CASE("C-API numeric inequality agrees with equality",
          "[C-API WASM surface][Number]") {
    kth_number_mut_t a = NULL;
    kth_number_mut_t b = NULL;
    REQUIRE(kth_vm_number_from_int(3, &a) == kth_ec_success);
    REQUIRE(kth_vm_number_from_int(4, &b) == kth_ec_success);
    REQUIRE_FALSE(kth_vm_number_not_equal(a, a));
    REQUIRE(kth_vm_number_not_equal(a, b));
    REQUIRE(kth_vm_number_not_equal(b, a));
    REQUIRE_FALSE(kth_vm_number_equals(a, b));
    kth_vm_number_destruct(b);
    kth_vm_number_destruct(a);

    kth_big_number_mut_t x = kth_vm_big_number_construct_from_value(-3);
    kth_big_number_mut_t y = kth_vm_big_number_construct_from_value(4);
    REQUIRE(x != NULL);
    REQUIRE(y != NULL);
    REQUIRE_FALSE(kth_vm_big_number_not_equal(x, x));
    REQUIRE(kth_vm_big_number_not_equal(x, y));
    REQUIRE(kth_vm_big_number_not_equal(y, x));
    REQUIRE_FALSE(kth_vm_big_number_equals(x, y));
    kth_vm_big_number_destruct(y);
    kth_vm_big_number_destruct(x);
}
