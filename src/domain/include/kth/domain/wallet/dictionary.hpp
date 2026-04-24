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

// Function-shaped accessors over the `language::` globals above.
// The existing `extern const` names are variables, not callables, so
// a binding generator can't bind them directly; the factories below
// give each built-in wordlist a function surface at the same
// namespace level. They forward by reference and cost nothing at
// runtime (every compiler inlines the return statement).
namespace languages {

inline dictionary const& en()      noexcept { return language::en; }
inline dictionary const& es()      noexcept { return language::es; }
inline dictionary const& ja()      noexcept { return language::ja; }
inline dictionary const& it()      noexcept { return language::it; }
inline dictionary const& fr()      noexcept { return language::fr; }
inline dictionary const& cs()      noexcept { return language::cs; }
inline dictionary const& ru()      noexcept { return language::ru; }
inline dictionary const& uk()      noexcept { return language::uk; }
inline dictionary const& zh_Hans() noexcept { return language::zh_Hans; }
inline dictionary const& zh_Hant() noexcept { return language::zh_Hant; }

// The union of every built-in BIP39 wordlist; the default lexicon for
// `validate_mnemonic(word_list, dictionary_list)` when the caller
// doesn't know which language produced the mnemonic.
inline dictionary_list const& all() noexcept { return language::all; }

} // namespace languages

} // namespace kth::domain::wallet

#endif // KTH_DOMAIN_WALLET_DICTIONARY_HPP_
