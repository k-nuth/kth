// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/wallet/wallet_manager.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_error_code_t kth_wallet_decrypt_seed(
    char const* password,
    kth_encrypted_seed_t const* encrypted_seed,
    kth_longhash_t** out_decrypted_seed) {

    auto seed_cpp = kth::to_array(encrypted_seed->hash);

    auto res = kth::domain::wallet::decrypt_seed(
        std::string(password),
        seed_cpp
    );

    if ( ! res) {
        return kth::to_c_err(res.error());
    }

    *out_decrypted_seed = new kth_longhash_t;
    kth::copy_c_hash(res.value(), *out_decrypted_seed);

    return kth_ec_success;
}

} // extern "C"
