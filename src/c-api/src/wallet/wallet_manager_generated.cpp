// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/wallet_manager.h>

#include <array>
#include <string>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

extern "C" {

kth_error_code_t kth_wallet_decrypt_seed(char const* password, kth_encrypted_seed_t const* encrypted_seed, kth_longhash_t** out_decrypted_seed) {
    KTH_PRECONDITION(password != nullptr);
    KTH_PRECONDITION(encrypted_seed != nullptr);
    KTH_PRECONDITION(out_decrypted_seed != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto result = kth::domain::wallet::decrypt_seed(
            std::string(password),
            std::to_array(encrypted_seed->hash));
        if ( ! result) return kth::to_c_err(result.error());
        auto* seed_owned = kth::make_owned<kth_longhash_t>();
        kth::copy_c_hash(*result, seed_owned);
        *out_decrypted_seed = seed_owned;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

} // extern "C"
