// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_PRIMITIVES_H_
#define KTH_CAPI_WALLET_PRIMITIVES_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTH_HD_CHAIN_CODE_SIZE 32
#define KTH_HD_KEY_SIZE 82
#define KTH_EC_COMPRESSED_SIZE 33
#define KTH_BITCOIN_EC_UNCOMPRESSED_SIZE 65
#define KTH_BITCOIN_EC_SECRET_SIZE 32
#define KTH_HD_FIRST_HARDENED_KEY (1 << 31)
#define KTH_WIF_UNCOMPRESSED_SIZE 37U
#define KTH_WIF_COMPRESSED_SIZE 38U
#define KTH_EC_SIGNATURE_SIZE 64
typedef struct {
    uint8_t data[KTH_HD_CHAIN_CODE_SIZE];
} kth_hd_chain_code_t;

typedef struct {
    uint8_t data[KTH_HD_KEY_SIZE];
} kth_hd_key_t;

typedef struct {
    uint8_t data[KTH_EC_COMPRESSED_SIZE];
} kth_ec_compressed_t;

typedef struct {
    uint8_t data[KTH_BITCOIN_EC_UNCOMPRESSED_SIZE];
} kth_ec_uncompressed_t;

typedef struct kth_ec_secret_t {
    uint8_t hash[KTH_BITCOIN_EC_SECRET_SIZE];
} kth_ec_secret_t;

typedef struct {
    uint8_t data[KTH_WIF_UNCOMPRESSED_SIZE];
} kth_wif_uncompressed_t;

typedef struct {
    uint8_t data[KTH_WIF_COMPRESSED_SIZE];
} kth_wif_compressed_t;


// Parsed ECDSA signature:
typedef struct {
    uint8_t data[KTH_EC_SIGNATURE_SIZE];
} kth_ec_signature_t;

// BIP137 signed-message envelope: 1 recovery-magic byte + 64-byte ECDSA sig.
#define KTH_MESSAGE_SIGNATURE_SIZE 65
typedef struct kth_message_signature_t {
    uint8_t data[KTH_MESSAGE_SIGNATURE_SIZE];
} kth_message_signature_t;

#define KTH_BITCOIN_PAYMENT_SIZE 25
typedef struct kth_payment_t {
    uint8_t hash[KTH_BITCOIN_PAYMENT_SIZE];
} kth_payment_t;

// BIP38 encrypted-key byte payloads. These are the base58-checked
// wire forms that `ek_private` / `ek_public` / `ek_token` wrap, each
// a fixed-size byte_array in the domain layer.
#define KTH_EK_PRIVATE_DECODED_SIZE 43
#define KTH_EK_PUBLIC_DECODED_SIZE 55
#define KTH_EK_TOKEN_DECODED_SIZE 53
typedef struct kth_encrypted_private_t {
    uint8_t data[KTH_EK_PRIVATE_DECODED_SIZE];
} kth_encrypted_private_t;

typedef struct kth_encrypted_public_t {
    uint8_t data[KTH_EK_PUBLIC_DECODED_SIZE];
} kth_encrypted_public_t;

typedef struct kth_encrypted_token_t {
    uint8_t data[KTH_EK_TOKEN_DECODED_SIZE];
} kth_encrypted_token_t;

// BIP38 random-input byte_arrays. These are caller-provided entropy
// sources consumed by `create_key_pair` / `create_token`; they never
// base58-encode, just pass through as fixed-size buffers.
#define KTH_EK_SEED_SIZE 24
#define KTH_EK_SALT_SIZE 4
#define KTH_EK_ENTROPY_SIZE 8
typedef struct kth_ek_seed_t {
    uint8_t data[KTH_EK_SEED_SIZE];
} kth_ek_seed_t;

typedef struct kth_ek_salt_t {
    uint8_t data[KTH_EK_SALT_SIZE];
} kth_ek_salt_t;

typedef struct kth_ek_entropy_t {
    uint8_t data[KTH_EK_ENTROPY_SIZE];
} kth_ek_entropy_t;


typedef void* kth_ec_private_t;
typedef void* kth_ec_private_mut_t;
typedef void const* kth_ec_private_const_t;
typedef void* kth_ec_public_t;
typedef void* kth_ec_public_mut_t;
typedef void const* kth_ec_public_const_t;
typedef void* kth_hd_private_t;
typedef void* kth_hd_private_mut_t;
typedef void const* kth_hd_private_const_t;
typedef void* kth_hd_public_t;
typedef void* kth_hd_public_mut_t;
typedef void const* kth_hd_public_const_t;

#ifdef __cplusplus
} // extern "C"
#endif


#endif /* KTH_CAPI_WALLET_PRIMITIVES_H_ */
