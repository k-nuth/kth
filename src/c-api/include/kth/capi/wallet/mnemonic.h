// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_MNEMONIC_H_
#define KTH_CAPI_WALLET_MNEMONIC_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

/** @return Owned `kth_string_list_mut_t`. Caller must release with `kth_core_string_list_destruct`. */
KTH_EXPORT KTH_OWNED
kth_string_list_mut_t kth_wallet_mnemonic_create_mnemonic(uint8_t const* entropy, kth_size_t n, kth_dictionary_const_t lexicon);

KTH_EXPORT
kth_bool_t kth_wallet_mnemonic_validate_mnemonic_dictionary(kth_string_list_const_t words, kth_dictionary_const_t lexicon);

KTH_EXPORT
kth_bool_t kth_wallet_mnemonic_validate_mnemonic_dictionary_list(kth_string_list_const_t mnemonic, kth_dictionary_list_const_t lexicons);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_MNEMONIC_H_
