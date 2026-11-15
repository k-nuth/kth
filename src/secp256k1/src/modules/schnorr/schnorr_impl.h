/***********************************************************************
 * Copyright (c) 2017 Amaury SÉCHET                                    *
 * Distributed under the MIT software license, see the accompanying    *
 * file COPYING or http://www.opensource.org/licenses/mit-license.php. *
 ***********************************************************************/

/*
 * BCH's custom EC-Schnorr-SHA256 scheme (not BIP340). Ported from the older
 * libsecp256k1 fork to this modern base:
 *   - secp256k1_ecmult no longer takes an ecmult_context (it is static now).
 *   - secp256k1_fe_is_quad_var -> secp256k1_fe_is_square_var (same predicate).
 *   - secp256k1_gej_has_quad_y_var was removed upstream; it is inlined here as
 *     is_square(R.y * R.z), which equals is_square(R.y / R.z^3) (the affine y).
 *   - secp256k1_fe_set_b32 -> secp256k1_fe_set_b32_limit (rejects r >= p).
 * The scheme itself is unchanged.
 */

#ifndef SECP256K1_SCHNORR_IMPL_H
#define SECP256K1_SCHNORR_IMPL_H

#include <stdlib.h>
#include <string.h>

#include "schnorr.h"
#include "../../field.h"
#include "../../group.h"
#include "../../hash.h"
#include "../../ecmult.h"
#include "../../ecmult_gen.h"
#include "../../ecmult_impl.h"
#include "../../eckey.h"
#include "../../scratch.h"
#include "../../util.h"

/**
 * Custom Schnorr-based signature scheme.
 *
 * Signing:
 *   Compute point R = k * G. Negate nonce if R.y is not a quadratic residue.
 *   Compute scalar e = Hash(R.x || compressed(P) || m) mod n.
 *   Compute scalar s = k + e * x. The signature is (R.x, s).
 *
 * Verification:
 *   Signature is invalid if s >= n or r >= p.
 *   Compute scalar e = Hash(r || compressed(P) || m) mod n.
 *   Compute point R = s * G - e * P. Reject if R is infinity or R.y is not a
 *   quadratic residue. Signature is valid if the serialization of R.x == r.
 */
static int secp256k1_schnorr_compute_e(
    const secp256k1_hash_ctx *hash_ctx,
    secp256k1_scalar* e,
    const unsigned char *r,
    secp256k1_ge *p,
    const unsigned char *msg32
) {
    int overflow = 0;
    secp256k1_sha256 sha;
    unsigned char buf[33];
    secp256k1_sha256_initialize(&sha);

    /* R.x */
    secp256k1_sha256_write(hash_ctx, &sha, r, 32);

    /* compressed P */
    secp256k1_eckey_pubkey_serialize33(p, buf);
    secp256k1_sha256_write(hash_ctx, &sha, buf, 33);

    /* msg */
    secp256k1_sha256_write(hash_ctx, &sha, msg32, 32);

    /* compute e */
    secp256k1_sha256_finalize(hash_ctx, &sha, buf);
    secp256k1_scalar_set_b32(e, buf, &overflow);
    return !overflow & !secp256k1_scalar_is_zero(e);
}

/* is_square(a.y * a.z) == is_square(a.y / a.z^3) == "R.y is a quadratic
 * residue" for the affine y of a non-infinite Jacobian point. */
static int secp256k1_schnorr_gej_has_quad_y_var(const secp256k1_gej *a) {
    secp256k1_fe yz;
    if (a->infinity) {
        return 0;
    }
    secp256k1_fe_mul(&yz, &a->y, &a->z);
    secp256k1_fe_normalize_var(&yz);
    return secp256k1_fe_is_square_var(&yz);
}

static int secp256k1_schnorr_sig_verify(
    const secp256k1_hash_ctx *hash_ctx,
    const unsigned char *sig64,
    secp256k1_ge *pubkey,
    const unsigned char *msg32
) {
    secp256k1_gej Pj, Rj;
    secp256k1_fe Rx;
    secp256k1_scalar e, s;
    int overflow;

    if (secp256k1_ge_is_infinity(pubkey)) {
        return 0;
    }

    /* Extract s */
    overflow = 0;
    secp256k1_scalar_set_b32(&s, sig64 + 32, &overflow);
    if (overflow) {
        return 0;
    }

    /* Extract R.x (rejects r >= p) */
    if (!secp256k1_fe_set_b32_limit(&Rx, sig64)) {
        return 0;
    }

    /* Compute e */
    secp256k1_schnorr_compute_e(hash_ctx, &e, sig64, pubkey, msg32);

    /* Compute R = s*G - e*P */
    secp256k1_scalar_negate(&e, &e);
    secp256k1_gej_set_ge(&Pj, pubkey);
    secp256k1_ecmult(&Rj, &Pj, &e, &s);
    if (secp256k1_gej_is_infinity(&Rj)) {
        return 0;
    }

    /* Check that R.x is what we expect */
    if (!secp256k1_gej_eq_x_var(&Rx, &Rj)) {
        return 0;
    }

    /* Check that jacobi(R.y) is 1 */
    if (!secp256k1_schnorr_gej_has_quad_y_var(&Rj)) {
        return 0;
    }

    /* All good, we have a valid signature. */
    return 1;
}

/*
 * Batch verification.
 *
 * Each valid signature i satisfies R_i = s_i*G - e_i*P_i where R_i is the
 * unique curve point with x = r_i and a square y (p = 3 mod 4, so exactly one
 * of y, -y is a quadratic residue). With random 128-bit a_i (a_0 = 1) the
 * batch computes
 *
 *   T = (sum_i a_i*s_i)*G + sum_i (-a_i*e_i)*P_i + sum_{i>0} a_i*(-R_i)
 *
 * and accepts iff T is that lift of r_0, checked the way the single verifier
 * checks its R: T is not infinity, T.x == r_0 and T.y is a square. That is
 * sum_i a_i*(s_i*G - e_i*P_i - R_i) == infinity without lifting r_0. The
 * a_i are derived from a hash of every input plus the caller's seed, so an
 * attacker who fixes the inputs first succeeds with probability ~2^-128.
 *
 * The multi-scalar multiplication keeps the 128-bit a_i whole: only the full
 * width scalars (-a_i*e_i and the G scalar) are split with the endomorphism,
 * so the n-1 R_i terms cost one 128-bit stream each instead of two. Small
 * batches use Strauss' algorithm with the precomputed G tables, larger ones
 * Pippenger's bucket method over 3n points, in chunks of bounded size.
 */

/* Batches up to this size use Strauss' algorithm, larger ones Pippenger's. */
#ifndef SECP256K1_SCHNORR_BATCH_STRAUSS_MAX
#  define SECP256K1_SCHNORR_BATCH_STRAUSS_MAX 16
#endif
/* Signatures per Pippenger chunk (bounds the memory of one call). */
#ifndef SECP256K1_SCHNORR_BATCH_CHUNK
#  define SECP256K1_SCHNORR_BATCH_CHUNK 8192
#endif
/* Bucket entries Pippenger reduces at once (several digit positions share
 * each round's inversion while they fit). */
#ifndef SECP256K1_SCHNORR_BATCH_GROUP_ITEMS
#  define SECP256K1_SCHNORR_BATCH_GROUP_ITEMS 131072
#endif
/* Digit positions in a group from which their bucket sums are combined in
 * affine coordinates (fewer cannot amortise the inversion of each step). */
#ifndef SECP256K1_SCHNORR_BATCH_AFFINE_SUMS
#  define SECP256K1_SCHNORR_BATCH_AFFINE_SUMS 6
#endif
/* Odd multiples per variable base in the Strauss path. */
#define SECP256K1_SCHNORR_STRAUSS_TABLE ECMULT_TABLE_SIZE(WINDOW_A)

/* Lifts x to the point (x, y) with y a quadratic residue. Returns 0 if x is
 * not a valid x coordinate. (p + 1)/4 is even, so the root fe_sqrt computes,
 * a^((p+1)/4) = (a^((p+1)/8))^2, is itself the square one of y, -y. */
static int secp256k1_schnorr_lift_x_quad(secp256k1_ge *r, const secp256k1_fe *x) {
    secp256k1_fe c, y;
    secp256k1_fe_sqr(&c, x);
    secp256k1_fe_mul(&c, &c, x);
    secp256k1_fe_add_int(&c, SECP256K1_B);
    if (!secp256k1_fe_sqrt(&y, &c)) {
        return 0;
    }
    secp256k1_ge_set_xy(r, x, &y);
    return 1;
}

/* Pippenger bucket window for a given number of (at most 128-bit) entries. */
static int secp256k1_schnorr_batch_window(size_t entries) {
    if (entries <= 50) {
        return 3;
    } else if (entries <= 150) {
        return 4;
    } else if (entries <= 250) {
        return 5;
    } else if (entries <= 650) {
        return 6;
    } else if (entries <= 2000) {
        return 7;
    } else if (entries <= 4000) {
        return 8;
    } else {
        return 9;
    }
}

typedef struct {
    int8_t wnaf_1[129];
    int8_t wnaf_lam[129];
    int bits_1;
    int bits_lam;
} secp256k1_schnorr_strauss_point;

/* r = ng*G + sum_{j < n_full} sc[j]*pt[j] + sum_{j >= n_full} sc[j]*pt[j],
 * where the scalars from n_full on are below 2^128 and are not split. pre and
 * aux hold SECP256K1_SCHNORR_STRAUSS_TABLE entries per point, ps one per point. */
static void secp256k1_schnorr_batch_strauss(secp256k1_gej *r, const secp256k1_ge *pt, const secp256k1_scalar *sc, size_t n_full, size_t num, const secp256k1_scalar *ng, secp256k1_ge *pre, secp256k1_fe *aux, secp256k1_schnorr_strauss_point *ps) {
    const size_t tbl = SECP256K1_SCHNORR_STRAUSS_TABLE;
    secp256k1_ge tmpa;
    secp256k1_fe Z;
    secp256k1_scalar ng_1, ng_128;
    int wnaf_ng_1[129], wnaf_ng_128[129];
    int bits_ng_1, bits_ng_128, bits = 0, i;
    size_t np;

    secp256k1_fe_set_int(&Z, 1);
    for (np = 0; np < num; np++) {
        secp256k1_gej tmp;
        if (np < n_full) {
            secp256k1_scalar s_1, s_lam;
            secp256k1_scalar_split_lambda(&s_1, &s_lam, &sc[np]);
            ps[np].bits_1 = secp256k1_ecmult_wnaf_small(ps[np].wnaf_1, 129, &s_1, WINDOW_A);
            ps[np].bits_lam = secp256k1_ecmult_wnaf_small(ps[np].wnaf_lam, 129, &s_lam, WINDOW_A);
        } else {
            ps[np].bits_1 = secp256k1_ecmult_wnaf_small(ps[np].wnaf_1, 129, &sc[np], WINDOW_A);
            ps[np].bits_lam = 0;
        }
        if (ps[np].bits_1 > bits) {
            bits = ps[np].bits_1;
        }
        if (ps[np].bits_lam > bits) {
            bits = ps[np].bits_lam;
        }
        /* Odd multiples on a shared Z (see secp256k1_ecmult_strauss_wnaf);
         * the inputs are affine, so the ratio to the previous table needs no
         * correction for the input's z. */
        secp256k1_gej_set_ge(&tmp, &pt[np]);
        if (np) {
            secp256k1_gej_rescale(&tmp, &Z);
        }
        secp256k1_ecmult_odd_multiples_table(tbl, pre + np * tbl, aux + np * tbl, &Z, &tmp);
    }
    if (num) {
        secp256k1_ge_table_set_globalz(tbl * num, pre, aux);
    }
    for (np = 0; np < n_full; np++) {
        size_t j;
        for (j = 0; j < tbl; j++) {
            secp256k1_fe_mul(&aux[np * tbl + j], &pre[np * tbl + j].x, &secp256k1_const_beta);
        }
    }

    secp256k1_scalar_split_128(&ng_1, &ng_128, ng);
    bits_ng_1 = secp256k1_ecmult_wnaf(wnaf_ng_1, 129, &ng_1, WINDOW_G);
    bits_ng_128 = secp256k1_ecmult_wnaf(wnaf_ng_128, 129, &ng_128, WINDOW_G);
    if (bits_ng_1 > bits) {
        bits = bits_ng_1;
    }
    if (bits_ng_128 > bits) {
        bits = bits_ng_128;
    }

    secp256k1_gej_set_infinity(r);
    for (i = bits - 1; i >= 0; i--) {
        int n;
        secp256k1_gej_double_var(r, r, NULL);
        for (np = 0; np < num; np++) {
            if (i < ps[np].bits_1 && (n = ps[np].wnaf_1[i])) {
                secp256k1_ecmult_table_get_ge(&tmpa, pre + np * tbl, n, WINDOW_A);
                secp256k1_gej_add_ge_var(r, r, &tmpa, NULL);
            }
            if (i < ps[np].bits_lam && (n = ps[np].wnaf_lam[i])) {
                secp256k1_ecmult_table_get_ge_lambda(&tmpa, pre + np * tbl, aux + np * tbl, n, WINDOW_A);
                secp256k1_gej_add_ge_var(r, r, &tmpa, NULL);
            }
        }
        if (i < bits_ng_1 && (n = wnaf_ng_1[i])) {
            secp256k1_ecmult_table_get_ge_storage(&tmpa, secp256k1_pre_g, n, WINDOW_G);
            secp256k1_gej_add_zinv_var(r, r, &tmpa, &Z);
        }
        if (i < bits_ng_128 && (n = wnaf_ng_128[i])) {
            secp256k1_ecmult_table_get_ge_storage(&tmpa, secp256k1_pre_g_128, n, WINDOW_G);
            secp256k1_gej_add_zinv_var(r, r, &tmpa, &Z);
        }
    }
    if (!secp256k1_gej_is_infinity(r)) {
        secp256k1_fe_mul(&r->z, &r->z, &Z);
    }
}

/* Scratch of secp256k1_schnorr_batch_pippenger for up to num entries, a
 * window and the number of digit positions reduced together (group). The
 * sizes are given by secp256k1_schnorr_pippenger_alloc. */
typedef struct {
    int window;
    size_t group;
    int *wnaf;              /* num * WNAF_SIZE(window + 1) digits */
    int *skew;              /* num */
    uint32_t *slot;         /* (group + 1) * num: (entry << 1) | negate, by bucket */
    size_t *cnt;            /* group << window: points left in each bucket */
    size_t *off;            /* (group << window) + 1: bucket starts in slot/work */
    secp256k1_ge *work;     /* (group + 1) * num: the bucket contents */
    secp256k1_fe *den;      /* (group + 1) * num / 2: denominators, then inverses */
    secp256k1_fe *pre;      /* (group + 1) * num / 2: prefix products of den */
    unsigned char *special; /* (group + 1) * num / 2: the pair has equal x */
    secp256k1_ge *sum;      /* 2 * group: running and total sums per digit */
    secp256k1_ge **dst;     /* 2 * group: the additions of one aggregation step */
    const secp256k1_ge **src;
} secp256k1_schnorr_pippenger_scratch;

static void secp256k1_schnorr_pippenger_alloc(const secp256k1_callback *error_callback, secp256k1_schnorr_pippenger_scratch *s, size_t num, int window, size_t group) {
    size_t n_wnaf = WNAF_SIZE(window + 1), nb = (size_t)1 << window, items, pairs;
    if (group > n_wnaf) {
        group = n_wnaf;
    }
    if (group == 0) {
        group = 1;
    }
    items = (group + 1) * num;
    pairs = items / 2 + 1;
    if (pairs < 2 * group) {
        pairs = 2 * group;
    }
    s->window = window;
    s->group = group;
    s->wnaf = (int *)checked_malloc(error_callback, num * n_wnaf * sizeof(int));
    s->skew = (int *)checked_malloc(error_callback, num * sizeof(int));
    s->slot = (uint32_t *)checked_malloc(error_callback, items * sizeof(uint32_t));
    s->cnt = (size_t *)checked_malloc(error_callback, group * nb * sizeof(size_t));
    s->off = (size_t *)checked_malloc(error_callback, (group * nb + 1) * sizeof(size_t));
    s->work = (secp256k1_ge *)checked_malloc(error_callback, items * sizeof(secp256k1_ge));
    s->den = (secp256k1_fe *)checked_malloc(error_callback, pairs * sizeof(secp256k1_fe));
    s->pre = (secp256k1_fe *)checked_malloc(error_callback, pairs * sizeof(secp256k1_fe));
    s->special = (unsigned char *)checked_malloc(error_callback, pairs);
    s->sum = (secp256k1_ge *)checked_malloc(error_callback, 2 * group * sizeof(secp256k1_ge));
    s->dst = (secp256k1_ge **)checked_malloc(error_callback, 2 * group * sizeof(secp256k1_ge *));
    s->src = (const secp256k1_ge **)checked_malloc(error_callback, 2 * group * sizeof(secp256k1_ge *));
}

static void secp256k1_schnorr_pippenger_free(secp256k1_schnorr_pippenger_scratch *s) {
    free(s->wnaf);
    free(s->skew);
    free(s->slot);
    free(s->cnt);
    free(s->off);
    free(s->work);
    free(s->den);
    free(s->pre);
    free(s->special);
    free(s->sum);
    free(s->dst);
    free(s->src);
}

/* r = a + b for affine a, b with a.x != b.x, given inv = 1/(b.x - a.x). The
 * x coordinates have magnitude 1, the y coordinates at most 3; the result has
 * magnitude 1. */
static void secp256k1_schnorr_ge_add_inv(secp256k1_ge *r, const secp256k1_ge *a, const secp256k1_ge *b, const secp256k1_fe *inv) {
    secp256k1_fe lambda, x3, y3, t;
    secp256k1_fe_negate(&t, &a->y, 3);
    secp256k1_fe_add(&t, &b->y);
    secp256k1_fe_mul(&lambda, &t, inv);
    secp256k1_fe_sqr(&x3, &lambda);
    secp256k1_fe_negate(&t, &a->x, 1);
    secp256k1_fe_add(&x3, &t);
    secp256k1_fe_negate(&t, &b->x, 1);
    secp256k1_fe_add(&x3, &t);
    secp256k1_fe_normalize_weak(&x3);
    secp256k1_fe_negate(&t, &x3, 1);
    secp256k1_fe_add(&t, &a->x);
    secp256k1_fe_mul(&y3, &lambda, &t);
    secp256k1_fe_negate(&t, &a->y, 3);
    secp256k1_fe_add(&y3, &t);
    secp256k1_fe_normalize_weak(&y3);
    secp256k1_ge_set_xy(r, &x3, &y3);
}

/* r = a + b for affine a, b with a.x == b.x: 2a, or infinity if b = -a. */
static void secp256k1_schnorr_ge_add_equal_x(secp256k1_ge *r, const secp256k1_ge *a, const secp256k1_ge *b) {
    secp256k1_fe dy;
    secp256k1_fe_negate(&dy, &a->y, 3);
    secp256k1_fe_add(&dy, &b->y);
    if (secp256k1_fe_normalizes_to_zero_var(&dy)) {
        secp256k1_gej d;
        secp256k1_gej_set_ge(&d, a);
        secp256k1_gej_double_var(&d, &d, NULL);
        secp256k1_ge_set_gej_var(r, &d);
        secp256k1_fe_normalize_weak(&r->x);
        secp256k1_fe_normalize_weak(&r->y);
    } else {
        secp256k1_ge_set_infinity(r);
    }
}

/* Computes 1/den[k] for k < n in place (Montgomery's trick); pre is scratch. */
static void secp256k1_schnorr_fe_inv_all_var(secp256k1_fe *den, secp256k1_fe *pre, size_t n) {
    secp256k1_fe inv, t;
    size_t k;
    if (n == 0) {
        return;
    }
    pre[0] = den[0];
    for (k = 1; k < n; k++) {
        secp256k1_fe_mul(&pre[k], &pre[k - 1], &den[k]);
    }
    secp256k1_fe_inv_var(&inv, &pre[n - 1]);
    for (k = n - 1; k > 0; k--) {
        secp256k1_fe_mul(&t, &inv, &pre[k - 1]);
        secp256k1_fe_mul(&inv, &inv, &den[k]);
        den[k] = t;
    }
    den[0] = inv;
}

/* *dst[j] += *src[j] for j < n in affine coordinates with one inversion. The
 * additions are applied in order and each reads its operands as they were
 * before the call, so a src may be a later dst. */
static void secp256k1_schnorr_ge_add_batch(secp256k1_ge *const *dst, const secp256k1_ge *const *src, size_t n, const secp256k1_schnorr_pippenger_scratch *s) {
    size_t j, n_den = 0;
    for (j = 0; j < n; j++) {
        if (dst[j]->infinity || src[j]->infinity) {
            continue;
        }
        secp256k1_fe_negate(&s->den[n_den], &dst[j]->x, 1);
        secp256k1_fe_add(&s->den[n_den], &src[j]->x);
        s->special[n_den] = secp256k1_fe_normalizes_to_zero_var(&s->den[n_den]);
        if (s->special[n_den]) {
            secp256k1_fe_set_int(&s->den[n_den], 1);
        }
        n_den++;
    }
    secp256k1_schnorr_fe_inv_all_var(s->den, s->pre, n_den);
    n_den = 0;
    for (j = 0; j < n; j++) {
        secp256k1_ge p = *dst[j];
        if (src[j]->infinity) {
            continue;
        } else if (p.infinity) {
            *dst[j] = *src[j];
        } else if (s->special[n_den++]) {
            secp256k1_schnorr_ge_add_equal_x(dst[j], &p, src[j]);
        } else {
            secp256k1_schnorr_ge_add_inv(dst[j], &p, src[j], &s->den[n_den - 1]);
        }
    }
}

/* r = sum_j sc[j]*pt[j] for scalars below 2^128 and points with x of
 * magnitude 1 and y of magnitude at most 2, with Pippenger's bucket method
 * (the digits and bucket combination of secp256k1_ecmult_pippenger_wnaf).
 * The points of each bucket are summed in affine coordinates as a balanced
 * tree: every round adds disjoint pairs in all buckets of s->group digit
 * positions at once and shares one field inversion between all of them
 * (Montgomery's trick), which is cheaper than accumulating each bucket in
 * Jacobian coordinates. num must not exceed the size s was allocated for. */
static void secp256k1_schnorr_batch_pippenger(secp256k1_gej *r, const secp256k1_ge *pt, const secp256k1_scalar *sc, size_t num, const secp256k1_schnorr_pippenger_scratch *s) {
    const int window = s->window;
    const size_t n_wnaf = WNAF_SIZE(window + 1);
    const size_t nb = (size_t)1 << window;
    size_t np, b, lo, hi;

    for (np = 0; np < num; np++) {
        s->skew[np] = secp256k1_wnaf_fixed(&s->wnaf[np * n_wnaf], &sc[np], window + 1);
    }
    secp256k1_gej_set_infinity(r);

    /* Digit positions [lo, hi), from the top down. */
    for (hi = n_wnaf; hi > 0; hi = lo) {
        size_t n_buckets, total, i;
        int pairs;
        lo = hi > s->group ? hi - s->group : 0;
        n_buckets = (hi - lo) * nb;

        /* Sort the (signed) points of these digits into their buckets;
         * bucket (i - lo) * nb + k holds the points of digit i with |d| =
         * 2k + 1. */
        memset(s->cnt, 0, n_buckets * sizeof(size_t));
        for (np = 0; np < num; np++) {
            const int *d = &s->wnaf[np * n_wnaf];
            for (i = lo; i < hi; i++) {
                if (d[i]) {
                    s->cnt[(i - lo) * nb + ((size_t)((d[i] < 0 ? -d[i] : d[i]) - 1) >> 1)]++;
                }
            }
            if (lo == 0 && s->skew[np]) {
                s->cnt[0]++;
            }
        }
        s->off[0] = 0;
        for (b = 0; b < n_buckets; b++) {
            s->off[b + 1] = s->off[b] + s->cnt[b];
        }
        total = s->off[n_buckets];
        for (np = 0; np < num; np++) {
            const int *d = &s->wnaf[np * n_wnaf];
            for (i = lo; i < hi; i++) {
                if (d[i]) {
                    size_t buc = (i - lo) * nb + ((size_t)((d[i] < 0 ? -d[i] : d[i]) - 1) >> 1);
                    s->slot[s->off[buc + 1] - s->cnt[buc]] = ((uint32_t)np << 1) | (uint32_t)(d[i] < 0);
                    s->cnt[buc]--;
                }
            }
            if (lo == 0 && s->skew[np]) {
                /* The digits represent sc + skew: subtract the point once. */
                s->slot[s->off[1] - s->cnt[0]] = ((uint32_t)np << 1) | 1;
                s->cnt[0]--;
            }
        }
        for (b = 0; b < n_buckets; b++) {
            s->cnt[b] = s->off[b + 1] - s->off[b];
        }
        for (np = 0; np < total; np++) {
            secp256k1_ge *w = &s->work[np];
            *w = pt[s->slot[np] >> 1];
            if (s->slot[np] & 1) {
                secp256k1_fe_negate(&w->y, &w->y, 2);
            }
        }

        /* Halve every bucket per round until each holds at most one point. */
        do {
            size_t k, n_den = 0;
            pairs = 0;
            for (b = 0; b < n_buckets; b++) {
                const secp256k1_ge *base = &s->work[s->off[b]];
                for (k = 0; k + 1 < s->cnt[b]; k += 2) {
                    const secp256k1_ge *p = &base[k], *q = &base[k + 1];
                    pairs = 1;
                    if (p->infinity || q->infinity) {
                        continue;
                    }
                    secp256k1_fe_negate(&s->den[n_den], &p->x, 1);
                    secp256k1_fe_add(&s->den[n_den], &q->x);
                    s->special[n_den] = secp256k1_fe_normalizes_to_zero_var(&s->den[n_den]);
                    if (s->special[n_den]) {
                        secp256k1_fe_set_int(&s->den[n_den], 1);
                    }
                    n_den++;
                }
            }
            if (!pairs) {
                break;
            }
            secp256k1_schnorr_fe_inv_all_var(s->den, s->pre, n_den);
            n_den = 0;
            for (b = 0; b < n_buckets; b++) {
                secp256k1_ge *base = &s->work[s->off[b]];
                size_t c = s->cnt[b];
                if (c < 2) {
                    continue;
                }
                /* Pair k lands in slot k/2 (<= k): the additions read both
                 * operands before writing the result. */
                for (k = 0; k + 1 < c; k += 2) {
                    const secp256k1_ge *p = &base[k], *q = &base[k + 1];
                    secp256k1_ge *out = &base[k >> 1];
                    if (p->infinity) {
                        *out = *q;
                    } else if (q->infinity) {
                        *out = *p;
                    } else if (s->special[n_den]) {
                        n_den++;
                        secp256k1_schnorr_ge_add_equal_x(out, p, q);
                    } else {
                        secp256k1_schnorr_ge_add_inv(out, p, q, &s->den[n_den]);
                        n_den++;
                    }
                }
                if (c & 1) {
                    base[c >> 1] = base[c - 1];
                }
                s->cnt[b] = (c + 1) >> 1;
            }
        } while (pairs);

        /* Per digit: sum_k (2k+1)*bucket_k = 2T + S with S = sum_k bucket_k
         * and T = sum_{k>=1} sum_{k'>=k} bucket_k' (the running sum of
         * secp256k1_ecmult_pippenger_wnaf). */
        if (hi - lo >= SECP256K1_SCHNORR_BATCH_AFFINE_SUMS) {
            /* All digits of the group step together, from the top bucket
             * down, in affine coordinates: one inversion per step for T += S
             * (the S before the step) and S += bucket. */
            for (i = 0; i < 2 * (hi - lo); i++) {
                secp256k1_ge_set_infinity(&s->sum[i]);
            }
            for (b = nb; b-- > 0;) {
                size_t n_add = 0;
                for (i = 0; i < hi - lo; i++) {
                    s->dst[n_add] = &s->sum[2 * i + 1];
                    s->src[n_add] = &s->sum[2 * i];
                    n_add++;
                }
                for (i = 0; i < hi - lo; i++) {
                    size_t buc = i * nb + b;
                    if (s->cnt[buc]) {
                        s->dst[n_add] = &s->sum[2 * i];
                        s->src[n_add] = &s->work[s->off[buc]];
                        n_add++;
                    }
                }
                secp256k1_schnorr_ge_add_batch(s->dst, s->src, n_add, s);
            }
            /* r = 2^(window+1)*r + 2T + S per digit, from the top. */
            for (i = hi; i-- > lo;) {
                secp256k1_gej t;
                int j;
                for (j = 0; j <= window; j++) {
                    secp256k1_gej_double_var(r, r, NULL);
                }
                secp256k1_gej_set_ge(&t, &s->sum[2 * (i - lo) + 1]);
                secp256k1_gej_double_var(&t, &t, NULL);
                secp256k1_gej_add_ge_var(r, r, &s->sum[2 * (i - lo)], NULL);
                secp256k1_gej_add_var(r, r, &t, NULL);
            }
        } else {
            /* Too few digits to share an inversion: Jacobian, as in
             * secp256k1_ecmult_pippenger_wnaf. */
            for (i = hi; i-- > lo;) {
                const size_t first = (i - lo) * nb;
                secp256k1_gej running_sum;
                int j;
                for (j = 0; j < window; j++) {
                    secp256k1_gej_double_var(r, r, NULL);
                }
                secp256k1_gej_set_infinity(&running_sum);
                for (b = nb - 1; b > 0; b--) {
                    if (s->cnt[first + b]) {
                        secp256k1_gej_add_ge_var(&running_sum, &running_sum, &s->work[s->off[first + b]], NULL);
                    }
                    secp256k1_gej_add_var(r, r, &running_sum, NULL);
                }
                if (s->cnt[first]) {
                    secp256k1_gej_add_ge_var(&running_sum, &running_sum, &s->work[s->off[first]], NULL);
                }
                secp256k1_gej_double_var(r, r, NULL);
                secp256k1_gej_add_var(r, r, &running_sum, NULL);
            }
        }
    }
}

/* Weight i > 0: 16 bytes of SHA256(H(inputs) [|| seed] || le64(i / 2)), two
 * weights per digest. A zero weight (probability 2^-128) becomes 1. */
static void secp256k1_schnorr_batch_weight(const secp256k1_hash_ctx *hash_ctx, secp256k1_scalar *a, unsigned char *digest, const secp256k1_sha256 *base, size_t i) {
    unsigned char buf[32];
    if (i == 1 || (i & 1) == 0) {
        secp256k1_sha256 sha = *base;
        unsigned char idx[8];
        uint64_t k = (uint64_t)(i >> 1);
        size_t j;
        for (j = 0; j < 8; j++) {
            idx[j] = (unsigned char)(k >> (8 * j));
        }
        secp256k1_sha256_write(hash_ctx, &sha, idx, 8);
        secp256k1_sha256_finalize(hash_ctx, &sha, digest);
    }
    memset(buf, 0, 16);
    memcpy(buf + 16, digest + 16 * (i & 1), 16);
    secp256k1_scalar_set_b32(a, buf, NULL);
    if (secp256k1_scalar_is_zero(a)) {
        secp256k1_scalar_set_int(a, 1);
    }
}

static int secp256k1_schnorr_sig_verify_batch(
    const secp256k1_hash_ctx *hash_ctx,
    const secp256k1_callback *error_callback,
    const unsigned char *const *sig64s,
    const unsigned char *const *msg32s,
    const secp256k1_ge *pubkeys,
    size_t n,
    const unsigned char *seed32,
    size_t strauss_max,
    size_t chunk_max
) {
    secp256k1_scalar *es, *scalars;
    secp256k1_ge *points;
    secp256k1_scalar g_sc;
    secp256k1_gej acc;
    secp256k1_fe rx0;
    secp256k1_sha256 sha, base;
    unsigned char inputs_hash[32], digest[32];
    size_t i, lo, chunk, max_entries;
    secp256k1_schnorr_pippenger_scratch pip;
    int ret = 1;

    if (n == 0) {
        return 1;
    }
    es = (secp256k1_scalar *)checked_malloc(error_callback, n * sizeof(secp256k1_scalar));

    /* Structural checks (as in secp256k1_schnorr_sig_verify) and challenges.
     * e_i commits to r_i, P_i and the message, so hashing (e_i, s_i) commits
     * the weights to every input. */
    secp256k1_sha256_initialize(&sha);
    for (i = 0; i < n; i++) {
        secp256k1_scalar s;
        secp256k1_fe rx;
        secp256k1_ge p = pubkeys[i];
        unsigned char eb[32];
        int overflow = 0;
        secp256k1_scalar_set_b32(&s, sig64s[i] + 32, &overflow);
        if (overflow || secp256k1_ge_is_infinity(&p) || !secp256k1_fe_set_b32_limit(&rx, sig64s[i])) {
            free(es);
            return 0;
        }
        if (i == 0) {
            rx0 = rx;
        }
        secp256k1_schnorr_compute_e(hash_ctx, &es[i], sig64s[i], &p, msg32s[i]);
        secp256k1_scalar_get_b32(eb, &es[i]);
        secp256k1_sha256_write(hash_ctx, &sha, eb, 32);
        secp256k1_sha256_write(hash_ctx, &sha, sig64s[i] + 32, 32);
    }
    secp256k1_sha256_finalize(hash_ctx, &sha, inputs_hash);
    secp256k1_sha256_initialize(&base);
    secp256k1_sha256_write(hash_ctx, &base, inputs_hash, 32);
    if (seed32 != NULL) {
        secp256k1_sha256_write(hash_ctx, &base, seed32, 32);
    }

    if (n <= strauss_max) {
        chunk = n;
        max_entries = 2 * n - 1;
    } else {
        size_t n_chunks = CEIL_DIV(n, chunk_max);
        chunk = CEIL_DIV(n, n_chunks);
        max_entries = 3 * chunk + 2;
    }
    scalars = (secp256k1_scalar *)checked_malloc(error_callback, max_entries * sizeof(secp256k1_scalar));
    points = (secp256k1_ge *)checked_malloc(error_callback, max_entries * sizeof(secp256k1_ge));
    memset(&pip, 0, sizeof(pip));
    if (n > strauss_max) {
        secp256k1_schnorr_pippenger_alloc(error_callback, &pip, max_entries, secp256k1_schnorr_batch_window(max_entries), SECP256K1_SCHNORR_BATCH_GROUP_ITEMS / max_entries);
    }

    secp256k1_scalar_set_int(&g_sc, 0);
    secp256k1_gej_set_infinity(&acc);
    for (lo = 0; lo < n && ret; lo += chunk) {
        size_t m = n - lo < chunk ? n - lo : chunk;
        int last = lo + m == n;
        int strauss = n <= strauss_max;
        size_t n_full = 0, n_short, num = 0;
        secp256k1_gej res;

        /* Strauss: the P_i first, then the -R_i (unsplit). Pippenger: the
         * split pair of each P_i and -R_i, and in the last chunk the split
         * pair of G. */
        n_short = strauss ? m : 0;
        for (i = lo; i < lo + m; i++) {
            secp256k1_scalar a, t, s;
            secp256k1_ge r;
            secp256k1_fe rx;
            if (i == 0) {
                secp256k1_scalar_set_int(&a, 1);
            } else {
                secp256k1_schnorr_batch_weight(hash_ctx, &a, digest, &base, i);
            }
            secp256k1_scalar_set_b32(&s, sig64s[i] + 32, NULL);
            secp256k1_scalar_mul(&t, &a, &s);
            secp256k1_scalar_add(&g_sc, &g_sc, &t);
            secp256k1_scalar_mul(&t, &a, &es[i]);
            secp256k1_scalar_negate(&t, &t);

            if (strauss) {
                points[n_full] = pubkeys[i];
                scalars[n_full] = t;
                n_full++;
            } else {
                points[num] = pubkeys[i];
                scalars[num] = t;
                secp256k1_ecmult_endo_split(&scalars[num], &scalars[num + 1], &points[num], &points[num + 1]);
                num += 2;
            }
            if (i == 0) {
                continue;
            }
            secp256k1_fe_set_b32_mod(&rx, sig64s[i]);
            if (!secp256k1_schnorr_lift_x_quad(&r, &rx)) {
                ret = 0;
                break;
            }
            if (strauss) {
                secp256k1_ge_neg(&points[n_short], &r);
                scalars[n_short] = a;
                n_short++;
            } else {
                secp256k1_ge_neg(&points[num], &r);
                scalars[num] = a;
                num++;
            }
        }
        if (!ret) {
            break;
        }

        if (strauss) {
            size_t total = n_short;
            secp256k1_ge *pre;
            secp256k1_fe *aux;
            secp256k1_schnorr_strauss_point *ps;
            pre = (secp256k1_ge *)checked_malloc(error_callback, total * SECP256K1_SCHNORR_STRAUSS_TABLE * sizeof(secp256k1_ge));
            aux = (secp256k1_fe *)checked_malloc(error_callback, total * SECP256K1_SCHNORR_STRAUSS_TABLE * sizeof(secp256k1_fe));
            ps = (secp256k1_schnorr_strauss_point *)checked_malloc(error_callback, total * sizeof(secp256k1_schnorr_strauss_point));
            secp256k1_schnorr_batch_strauss(&res, points, scalars, n_full, total, &g_sc, pre, aux, ps);
            free(pre);
            free(aux);
            free(ps);
        } else {
            if (last) {
                points[num] = secp256k1_ge_const_g;
                scalars[num] = g_sc;
                secp256k1_ecmult_endo_split(&scalars[num], &scalars[num + 1], &points[num], &points[num + 1]);
                num += 2;
            }
            secp256k1_schnorr_batch_pippenger(&res, points, scalars, num, &pip);
        }
        secp256k1_gej_add_var(&acc, &acc, &res, NULL);
    }

    free(es);
    free(scalars);
    free(points);
    if (n > strauss_max) {
        secp256k1_schnorr_pippenger_free(&pip);
    }
    if (!ret) {
        return 0;
    }
    /* acc must be the lift of r_0, checked as secp256k1_schnorr_sig_verify
     * checks its R. */
    return !secp256k1_gej_is_infinity(&acc)
        && secp256k1_gej_eq_x_var(&rx0, &acc)
        && secp256k1_schnorr_gej_has_quad_y_var(&acc);
}

static int secp256k1_schnorr_compute_sig(
    const secp256k1_hash_ctx *hash_ctx,
    unsigned char *sig64,
    const unsigned char *msg32,
    secp256k1_scalar *k,
    secp256k1_ge *R,
    const secp256k1_scalar *privkey,
    secp256k1_ge *pubkey
) {
    secp256k1_scalar e, s;
    secp256k1_fe ry;

    if (secp256k1_scalar_is_zero(privkey) || secp256k1_scalar_is_zero(k)) {
        return 0;
    }

    if (secp256k1_ge_is_infinity(pubkey)) {
        return 0;
    }

    ry = R->y;
    secp256k1_fe_normalize_var(&ry);
    if (!secp256k1_fe_is_square_var(&ry)) {
        /* R's y coordinate is not a quadratic residue; negate the nonce. */
        secp256k1_scalar_negate(k, k);
    }

    secp256k1_fe_normalize(&R->x);
    secp256k1_fe_get_b32(sig64, &R->x);
    secp256k1_schnorr_compute_e(hash_ctx, &e, sig64, pubkey, msg32);
    secp256k1_scalar_mul(&s, &e, privkey);
    secp256k1_scalar_add(&s, &s, k);
    secp256k1_scalar_get_b32(sig64 + 32, &s);
    return 1;
}

static int secp256k1_schnorr_sig_generate_k(
    const secp256k1_hash_ctx *hash_ctx,
    secp256k1_scalar *k,
    const unsigned char *msg32,
    const secp256k1_scalar *privkey,
    secp256k1_nonce_function noncefp,
    const void *ndata
) {
    int overflow = 0;
    int ret = 0;
    unsigned int count = 0;
    unsigned char nonce32[32], seckey[32];

    /* Seed used to make sure we generate different values of k for schnorr */
    const unsigned char secp256k1_schnorr_algo16[17] = "Schnorr+SHA256  ";

    secp256k1_scalar_get_b32(seckey, privkey);
    while (1) {
        if (noncefp == NULL || noncefp == secp256k1_nonce_function_default) {
            ret = nonce_function_rfc6979_impl(hash_ctx, nonce32, msg32, seckey,
                secp256k1_schnorr_algo16, (void*)ndata, count++);
        } else {
            ret = noncefp(nonce32, msg32, seckey, secp256k1_schnorr_algo16, (void*)ndata, count++);
        }
        if (!ret) {
            break;
        }

        secp256k1_scalar_set_b32(k, nonce32, &overflow);
        if (!overflow && !secp256k1_scalar_is_zero(k)) {
            break;
        }

        secp256k1_scalar_clear(k);
    }

    memset(seckey, 0, 32);
    memset(nonce32, 0, 32);
    return ret;
}

static int secp256k1_schnorr_compute_k_R(
    const secp256k1_hash_ctx *hash_ctx,
    const secp256k1_ecmult_gen_context* ctx,
    secp256k1_scalar *k,
    secp256k1_ge *R,
    const unsigned char *msg32,
    const secp256k1_scalar *privkey,
    secp256k1_nonce_function noncefp,
    const void *ndata
) {
    if (!secp256k1_schnorr_sig_generate_k(hash_ctx, k, msg32, privkey, noncefp, ndata)) {
        return 0;
    }

    secp256k1_ecmult_gen_ge(ctx, R, k);
    return 1;
}

static int secp256k1_schnorr_sig_sign(
    const secp256k1_hash_ctx *hash_ctx,
    const secp256k1_ecmult_gen_context* ctx,
    unsigned char *sig64,
    const unsigned char *msg32,
    const secp256k1_scalar *privkey,
    secp256k1_ge *pubkey,
    secp256k1_nonce_function noncefp,
    const void *ndata
) {
    secp256k1_ge R;
    secp256k1_scalar k;
    int ret;

    if (secp256k1_scalar_is_zero(privkey)) {
        return 0;
    }

    if (!secp256k1_schnorr_compute_k_R(hash_ctx, ctx, &k, &R, msg32, privkey, noncefp, ndata)) {
        return 0;
    }

    ret = secp256k1_schnorr_compute_sig(hash_ctx, sig64, msg32, &k, &R, privkey, pubkey);
    secp256k1_scalar_clear(&k);
    return ret;
}

#endif
