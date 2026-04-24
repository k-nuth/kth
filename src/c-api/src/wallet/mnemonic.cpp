// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/mnemonic.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/mnemonic.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_string_list_mut_t kth_wallet_mnemonic_create_mnemonic(uint8_t const* entropy, kth_size_t n, kth_dictionary_const_t lexicon) {
    KTH_PRECONDITION(entropy != nullptr || n == 0);
    KTH_PRECONDITION(lexicon != nullptr);
    auto const entropy_cpp = kth::byte_span(entropy, kth::sz(n));
    auto const& lexicon_cpp = kth::cpp_ref<std::array<const char *, 2048>>(lexicon);
    auto const cpp_result = kth::domain::wallet::create_mnemonic(entropy_cpp, lexicon_cpp);
    return kth::leak_list<std::string>(cpp_result.begin(), cpp_result.end());
}

kth_bool_t kth_wallet_mnemonic_validate_mnemonic_dictionary(kth_string_list_const_t words, kth_dictionary_const_t lexicon) {
    KTH_PRECONDITION(words != nullptr);
    KTH_PRECONDITION(lexicon != nullptr);
    auto const& words_cpp = kth::cpp_ref<kth::string_list>(words);
    auto const& lexicon_cpp = kth::cpp_ref<std::array<const char *, 2048>>(lexicon);
    return kth::bool_to_int(kth::domain::wallet::validate_mnemonic(words_cpp, lexicon_cpp));
}

kth_bool_t kth_wallet_mnemonic_validate_mnemonic_dictionary_list(kth_string_list_const_t mnemonic, kth_dictionary_list_const_t lexicons) {
    KTH_PRECONDITION(mnemonic != nullptr);
    KTH_PRECONDITION(lexicons != nullptr);
    auto const& mnemonic_cpp = kth::cpp_ref<kth::string_list>(mnemonic);
    auto const& lexicons_cpp = kth::cpp_ref<std::vector<const std::array<const char *, 2048> *>>(lexicons);
    return kth::bool_to_int(kth::domain::wallet::validate_mnemonic(mnemonic_cpp, lexicons_cpp));
}

} // extern "C"
