// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/language.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/dictionary.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_dictionary_const_t kth_wallet_language_en(void) {
    return &(kth::domain::wallet::languages::en());
}

kth_dictionary_const_t kth_wallet_language_es(void) {
    return &(kth::domain::wallet::languages::es());
}

kth_dictionary_const_t kth_wallet_language_ja(void) {
    return &(kth::domain::wallet::languages::ja());
}

kth_dictionary_const_t kth_wallet_language_it(void) {
    return &(kth::domain::wallet::languages::it());
}

kth_dictionary_const_t kth_wallet_language_fr(void) {
    return &(kth::domain::wallet::languages::fr());
}

kth_dictionary_const_t kth_wallet_language_cs(void) {
    return &(kth::domain::wallet::languages::cs());
}

kth_dictionary_const_t kth_wallet_language_ru(void) {
    return &(kth::domain::wallet::languages::ru());
}

kth_dictionary_const_t kth_wallet_language_uk(void) {
    return &(kth::domain::wallet::languages::uk());
}

kth_dictionary_const_t kth_wallet_language_zh_Hans(void) {
    return &(kth::domain::wallet::languages::zh_Hans());
}

kth_dictionary_const_t kth_wallet_language_zh_Hant(void) {
    return &(kth::domain::wallet::languages::zh_Hant());
}

kth_dictionary_list_const_t kth_wallet_language_all(void) {
    return &(kth::domain::wallet::languages::all());
}

} // extern "C"
