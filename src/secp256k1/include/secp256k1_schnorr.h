#ifndef SECP256K1_SCHNORR_H
# define SECP256K1_SCHNORR_H

# include "secp256k1.h"

# ifdef __cplusplus
extern "C" {
# endif

/**
 * Verify a signature created by secp256k1_schnorr_sign.
 * Returns: 1: correct signature
 *          0: incorrect signature
 * Args:    ctx:       a secp256k1 context object, initialized for verification.
 * In:      sig64:     the 64-byte signature being verified (cannot be NULL)
 *          msg32:     the 32-byte message hash being verified (cannot be NULL)
 *          pubkey:    the public key to verify with (cannot be NULL)
 */
SECP256K1_API SECP256K1_WARN_UNUSED_RESULT int secp256k1_schnorr_verify(
  const secp256k1_context* ctx,
  const unsigned char *sig64,
  const unsigned char *msg32,
  const secp256k1_pubkey *pubkey
) SECP256K1_ARG_NONNULL(1) SECP256K1_ARG_NONNULL(2) SECP256K1_ARG_NONNULL(3) SECP256K1_ARG_NONNULL(4);

/**
 * Batch-verify n signatures created by secp256k1_schnorr_sign with a random
 * linear combination (one multi-scalar multiplication instead of n double
 * multiplications). The verdict is the conjunction of the n individual
 * secp256k1_schnorr_verify verdicts: a forged batch passes with probability
 * about 2^-128 per attempt, for any inputs chosen before the call.
 * Returns: 1: every signature is valid
 *          0: at least one signature is invalid (or n > 0 with NULL arrays)
 * Args:    ctx:     a secp256k1 context object (cannot be NULL)
 * In:      sig64s:  n pointers to 64-byte signatures
 *          msg32s:  n pointers to 32-byte message hashes
 *          pubkeys: n pointers to public keys
 *          n:       number of signatures (0 is allowed and returns 1)
 *          seed32:  32 bytes of fresh randomness mixed into the randomizers,
 *                   or NULL to derive them from the inputs alone (can be NULL)
 */
SECP256K1_API SECP256K1_WARN_UNUSED_RESULT int secp256k1_schnorr_verify_batch(
  const secp256k1_context* ctx,
  const unsigned char* const* sig64s,
  const unsigned char* const* msg32s,
  const secp256k1_pubkey* const* pubkeys,
  size_t n,
  const unsigned char* seed32
) SECP256K1_ARG_NONNULL(1);

/**
 * Create a signature using a custom EC-Schnorr-SHA256 construction. It
 * produces non-malleable 64-byte signatures which support batch validation,
 * and multiparty signing.
 * Returns: 1: signature created
 *          0: the nonce generation function failed, or the private key was
 *             invalid.
 * Args:    ctx:    pointer to a context object, initialized for signing
 *                  (cannot be NULL)
 * Out:     sig64:  pointer to a 64-byte array where the signature will be
 *                  placed (cannot be NULL)
 * In:      msg32:  the 32-byte message hash being signed (cannot be NULL)
 *          seckey: pointer to a 32-byte secret key (cannot be NULL)
 *          noncefp:pointer to a nonce generation function. If NULL,
 *                  secp256k1_nonce_function_default is used
 *          ndata:  pointer to arbitrary data used by the nonce generation
 *                  function (can be NULL)
 */
SECP256K1_API int secp256k1_schnorr_sign(
  const secp256k1_context *ctx,
  unsigned char *sig64,
  const unsigned char *msg32,
  const unsigned char *seckey,
  secp256k1_nonce_function noncefp,
  const void *ndata
) SECP256K1_ARG_NONNULL(1) SECP256K1_ARG_NONNULL(2) SECP256K1_ARG_NONNULL(3) SECP256K1_ARG_NONNULL(4);

# ifdef __cplusplus
}
# endif

#endif
