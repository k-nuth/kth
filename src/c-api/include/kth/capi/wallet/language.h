// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_WALLET_LANGUAGE_H_
#define KTH_CAPI_WALLET_LANGUAGE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/wallet/primitives.h>

#ifdef __cplusplus
extern "C" {
#endif

// Static utilities

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_en(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_es(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_ja(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_it(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_fr(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_cs(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_ru(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_uk(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_zh_Hans(void);

/** @return Borrowed `kth_dictionary_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_const_t kth_wallet_language_zh_Hant(void);

/** @return Borrowed `kth_dictionary_list_const_t` into program-lifetime static storage. Do not destruct. */
KTH_EXPORT
kth_dictionary_list_const_t kth_wallet_language_all(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_WALLET_LANGUAGE_H_
