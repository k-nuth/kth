// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <stdio.h>
#include <string.h>
#include <kth/capi/error.h>
#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/domain/chain/script.h>

static_assert(kth_ec_resolve_failed == 7, "preserve existing network error code");
static_assert(kth_ec_channel_timeout == 13, "preserve existing timeout error code");
static_assert(kth_ec_out_of_memory == 10000, "preserve C boundary allocation code");
static_assert(kth_ec_unhandled_exception == 10001, "preserve C boundary exception code");

static int check_error(kth_error_code_t actual, kth_error_code_t expected,
                       kth_payment_address_mut_t out, char const* input) {
    int failed = actual != expected || out != NULL;
    if (failed) fprintf(stderr, "%s: error %d, expected %d; output %p\n",
                        input, (int)actual, (int)expected, (void*)out);
    if (out != NULL) kth_wallet_payment_address_destruct(out);
    return failed;
}

static int same_encoding(kth_payment_address_const_t a, kth_payment_address_const_t b) {
    if (a == NULL || b == NULL) return 1;
    char* left = kth_wallet_payment_address_to_string(a);
    char* right = kth_wallet_payment_address_to_string(b);
    int const failed = left == NULL || right == NULL || strcmp(left, right) != 0;
    if (left != NULL) kth_core_destruct_string(left);
    if (right != NULL) kth_core_destruct_string(right);
    return failed;
}

int main(void) {
    struct invalid_case { char const* input; kth_error_code_t error; };
    struct invalid_case const cases[] = {
        {"", kth_ec_address_empty_input},
        {"!", kth_ec_address_invalid_character},
        {"1", kth_ec_address_invalid_base58_length},
        {"unknown:qq", kth_ec_address_invalid_prefix},
        {"bitcoincash:Qq", kth_ec_address_mixed_case},
        {"bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsq", kth_ec_address_invalid_checksum},
        {"bitcoincash:qppyysjzgfpyysjzgfpyysjzgfpyysjzgf9rfnekt9", kth_ec_address_invalid_padding},
        {"bitcoincash:sppyysjzgfpyysjzgfpyysjzgfpyysjzgg8z97tvk3", kth_ec_address_reserved_version},
        {"bitcoincash:yppyysjzgfpyysjzgfpyysjzgfpyysjzggc5ldue0t", kth_ec_address_unsupported_type},
        {"bitcoincash:qppyysjzgfpyysjzgfpyysjzgfpyysjzardwvhs4", kth_ec_address_invalid_hash_length},
        {"bitcoincash:q9pyysjzgfpyysjzgfpyysjzgfpyysjzgfpyysjzfacg43jk", kth_ec_address_unsupported_hash_size},
    };
    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        kth_payment_address_mut_t out = NULL;
        kth_error_code_t ec = kth_wallet_payment_address_parse_from_simple(cases[i].input, &out);
        failed += check_error(ec, cases[i].error, out, cases[i].input);
        /* Address errors must never alias network errors through raw casts. */
        if (ec == kth_ec_resolve_failed || ec == kth_ec_channel_timeout) ++failed;
    }
    kth_payment_address_mut_t out = NULL;
    kth_error_code_t ec = kth_wallet_payment_address_parse_from(
        "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl", kth_network_testnet, &out);
    failed += check_error(ec, kth_ec_address_network_mismatch, out, "wrong network");
    kth_payment_t payment = {0};
    out = NULL;
    ec = kth_wallet_payment_address_from_payment(&payment, &out);
    failed += check_error(ec, kth_ec_address_invalid_checksum, out, "payment checksum");
    out = NULL;
    ec = kth_wallet_payment_address_from_payment_unsafe(payment.hash, &out);
    failed += check_error(ec, kth_ec_address_invalid_checksum, out, "unsafe payment checksum");
    kth_script_mut_t script = kth_domain_chain_script_construct_default();
    if (script == NULL) return 1;
    out = NULL;
    ec = kth_wallet_payment_address_from_pay_public_key_hash_script(script, 0, &out);
    failed += check_error(ec, kth_ec_address_invalid_script, out, "not P2PKH");
    out = NULL;
    ec = kth_wallet_payment_address_from_script(script, 0xff, &out);
    failed += check_error(ec, kth_ec_address_unsupported_type, out, "script unsupported version");
    out = NULL;
    ec = kth_wallet_payment_address_from_script(script, 5, &out);
    if (ec != kth_ec_success || out == NULL) ++failed;
    if (out != NULL) kth_wallet_payment_address_destruct(out);
    kth_domain_chain_script_destruct(script);
    out = NULL;
    ec = kth_wallet_payment_address_parse_from_simple(
        "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl", &out);
    if (ec != kth_ec_success || out == NULL) ++failed;
    if (out != NULL) kth_wallet_payment_address_destruct(out);
    kth_shorthash_t short_hash = {0};
    out = NULL;
    ec = kth_wallet_payment_address_from_shorthash(&short_hash, 0xff, &out);
    failed += check_error(ec, kth_ec_address_unsupported_type, out, "unknown hash version");
    kth_hash_t hash = {0};
    out = NULL;
    ec = kth_wallet_payment_address_from_hash(&hash, 0, &out);
    failed += check_error(ec, kth_ec_address_unsupported_hash_size, out, "32-byte P2PKH");
    out = NULL;
    ec = kth_wallet_payment_address_from_hash_unsafe(hash.hash, 0, &out);
    failed += check_error(ec, kth_ec_address_unsupported_hash_size, out, "unsafe 32-byte P2PKH");
    out = NULL;
    ec = kth_wallet_payment_address_from_shorthash_unsafe(short_hash.hash, 0xff, &out);
    failed += check_error(ec, kth_ec_address_unsupported_type, out, "unsafe unknown version");
    out = NULL;
    kth_payment_address_mut_t raw = NULL;
    ec = kth_wallet_payment_address_from_shorthash(&short_hash, 0, &out);
    kth_error_code_t raw_ec = kth_wallet_payment_address_from_shorthash_unsafe(short_hash.hash, 0, &raw);
    if (ec != kth_ec_success || raw_ec != kth_ec_success) ++failed;
    failed += same_encoding(out, raw);
    if (out != NULL) kth_wallet_payment_address_destruct(out);
    if (raw != NULL) kth_wallet_payment_address_destruct(raw);
    out = NULL;
    raw = NULL;
    ec = kth_wallet_payment_address_from_hash(&hash, 5, &out);
    raw_ec = kth_wallet_payment_address_from_hash_unsafe(hash.hash, 5, &raw);
    if (ec != kth_ec_success || raw_ec != kth_ec_success) ++failed;
    failed += same_encoding(out, raw);
    if (out != NULL) kth_wallet_payment_address_destruct(out);
    if (raw != NULL) kth_wallet_payment_address_destruct(raw);

    out = NULL;
    ec = kth_wallet_payment_address_parse_from_simple(
        "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl", &out);
    if (ec != kth_ec_success || out == NULL) return 1;
    char* encoded = kth_wallet_payment_address_to_string(out);
    if (encoded == NULL || strcmp(encoded,
        "bitcoincash:qr7fzmep8g7h7ymfxy74lgc0v950j3r2959lhtxxsl") != 0) ++failed;
    if (encoded != NULL) kth_core_destruct_string(encoded);
    kth_wallet_payment_address_destruct(out);
    printf("payment address C error checks: %d failures\n", failed);
    return failed != 0;
}
