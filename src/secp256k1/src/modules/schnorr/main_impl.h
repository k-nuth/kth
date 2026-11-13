/**********************************************************************
 * Copyright (c) 2017 Amaury Séchet                                   *
 * Distributed under the MIT software license, see the accompanying   *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.*
 **********************************************************************/

#ifndef SECP256K1_MODULE_SCHNORR_MAIN
#define SECP256K1_MODULE_SCHNORR_MAIN

#include "../../../include/secp256k1_schnorr.h"
#include "schnorr_impl.h"

int secp256k1_schnorr_verify(
    const secp256k1_context* ctx,
    const unsigned char *sig64,
    const unsigned char *msg32,
    const secp256k1_pubkey *pubkey
) {
    secp256k1_ge q;
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(msg32 != NULL);
    ARG_CHECK(sig64 != NULL);
    ARG_CHECK(pubkey != NULL);

    /* Verification no longer needs an ecmult context: secp256k1_ecmult is
     * static in this base. */
    secp256k1_pubkey_load(ctx, &q, pubkey);
    return secp256k1_schnorr_sig_verify(secp256k1_get_hash_context(ctx), sig64, &q, msg32);
}

int secp256k1_schnorr_verify_batch(
    const secp256k1_context* ctx,
    const unsigned char* const* sig64s,
    const unsigned char* const* msg32s,
    const secp256k1_pubkey* const* pubkeys,
    size_t n,
    const unsigned char* seed32
) {
    secp256k1_ge *ges;
    size_t i;
    int ret;
    VERIFY_CHECK(ctx != NULL);
    if (n == 0) {
        return 1;
    }
    ARG_CHECK(sig64s != NULL);
    ARG_CHECK(msg32s != NULL);
    ARG_CHECK(pubkeys != NULL);
    if (n == 1) {
        return secp256k1_schnorr_verify(ctx, sig64s[0], msg32s[0], pubkeys[0]);
    }
    ges = (secp256k1_ge *)checked_malloc(&ctx->error_callback, n * sizeof(secp256k1_ge));
    for (i = 0; i < n; i++) {
        ARG_CHECK(sig64s[i] != NULL);
        ARG_CHECK(msg32s[i] != NULL);
        ARG_CHECK(pubkeys[i] != NULL);
        if (!secp256k1_pubkey_load(ctx, &ges[i], pubkeys[i])) {
            free(ges);
            return 0;
        }
    }
    ret = secp256k1_schnorr_sig_verify_batch(secp256k1_get_hash_context(ctx), &ctx->error_callback, sig64s, msg32s, ges, n, seed32, SECP256K1_SCHNORR_BATCH_STRAUSS_MAX, SECP256K1_SCHNORR_BATCH_CHUNK);
    free(ges);
    return ret;
}

int secp256k1_schnorr_sign(
    const secp256k1_context *ctx,
    unsigned char *sig64,
    const unsigned char *msg32,
    const unsigned char *seckey,
    secp256k1_nonce_function noncefp,
    const void *ndata
) {
    secp256k1_scalar sec;
    secp256k1_pubkey pubkey;
    secp256k1_ge p;
    int ret = 0;
    VERIFY_CHECK(ctx != NULL);
    ARG_CHECK(secp256k1_ecmult_gen_context_is_built(&ctx->ecmult_gen_ctx));
    ARG_CHECK(msg32 != NULL);
    ARG_CHECK(sig64 != NULL);
    ARG_CHECK(seckey != NULL);

    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, seckey)) {
        return 0;
    }

    secp256k1_pubkey_load(ctx, &p, &pubkey);
    secp256k1_scalar_set_b32(&sec, seckey, NULL);
    ret = secp256k1_schnorr_sig_sign(secp256k1_get_hash_context(ctx), &ctx->ecmult_gen_ctx, sig64, msg32, &sec, &p, noncefp, ndata);
    if (!ret) {
        memset(sig64, 0, 64);
    }

    secp256k1_scalar_clear(&sec);
    return ret;
}

#endif
