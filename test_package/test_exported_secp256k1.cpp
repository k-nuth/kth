#include <memory>
#include <secp256k1.h>
#include <secp256k1_preallocated.h>
#include <secp256k1_ecdh.h>
#include <secp256k1_recovery.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>
#include <secp256k1_ellswift.h>
#include <secp256k1_schnorr.h>

int main() {
    std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> context(
        secp256k1_context_create(SECP256K1_CONTEXT_NONE), secp256k1_context_destroy);
    unsigned char key[32] = {};
    unsigned char message[32] = {};
    unsigned char signature[64] = {};
    secp256k1_pubkey pubkey;
    key[31] = 1;

    if (!secp256k1_ec_pubkey_create(context.get(), &pubkey, key)
        || !secp256k1_schnorr_sign(context.get(), signature, message, key, nullptr, nullptr)
        || !secp256k1_schnorr_verify(context.get(), signature, message, &pubkey)) {
        return 1;
    }
    signature[0] ^= 1;
    return secp256k1_schnorr_verify(context.get(), signature, message, &pubkey) ? 1 : 0;
}
