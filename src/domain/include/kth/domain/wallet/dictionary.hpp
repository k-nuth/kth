// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_DICTIONARY_HPP_
#define KTH_DOMAIN_WALLET_DICTIONARY_HPP_

#include <array>
#include <cstddef>
#include <vector>

#include <kth/infrastructure/compat.hpp>

namespace kth::domain::wallet {

/**
 * A valid mnemonic dictionary has exactly this many words.
 */
static constexpr size_t dictionary_size = 2048;

/**
 * A dictionary for creating mnemonics.
 * The bip39 spec calls this a "wordlist".
 * This is a POD type, which means the compiler can write it directly
 * to static memory with no run-time overhead.
 */
using dictionary = std::array<char const*, dictionary_size>;

/**
 * A collection of candidate dictionaries for mnemonic validation.
 */
using dictionary_list = std::vector<dictionary const*>;

namespace language {
    // Individual built-in languages:
    extern const dictionary en;
    extern const dictionary es;
    extern const dictionary ja;
    extern const dictionary it;
    extern const dictionary fr;
    extern const dictionary cs;
    extern const dictionary ru;
    extern const dictionary uk;
    extern const dictionary zh_Hans;
    extern const dictionary zh_Hant;

    // All built-in languages:
    extern const dictionary_list all;
}

} // namespace kth::domain::wallet

#endif // KTH_DOMAIN_WALLET_DICTIONARY_HPP_
