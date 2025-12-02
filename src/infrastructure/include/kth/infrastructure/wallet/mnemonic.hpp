// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_WALLET_MNEMONIC_HPP
#define KTH_INFRASTUCTURE_WALLET_MNEMONIC_HPP

#include <cstddef>
#include <string>
#include <vector>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/string.hpp>
#include <kth/infrastructure/wallet/dictionary.hpp>

namespace kth::infrastructure::wallet {

/**
 * A valid mnemonic word count is evenly divisible by this number.
 */
static constexpr size_t mnemonic_word_multiple = 3;

/**
 * A valid seed byte count is evenly divisible by this number.
 */
static constexpr size_t mnemonic_seed_multiple = 4;

/**
 * Represents a mnemonic word list.
 */
using word_list = string_list;

/**
 * Create a new mnenomic (list of words) from provided entropy and a dictionary
 * selection. The mnemonic can later be converted to a seed for use in wallet
 * creation. Entropy byte count must be evenly divisible by 4.
 */
KI_API word_list create_mnemonic(byte_span entropy,
    dictionary const& lexicon=language::en);

/**
 * Checks a mnemonic against a dictionary to determine if the
 * words are spelled correctly and the checksum matches.
 * The words must have been created using mnemonic encoding.
 */
KI_API bool validate_mnemonic(word_list const& words,
    dictionary const& lexicon);

/**
 * Checks that a mnemonic is valid in at least one of the provided languages.
 */
KI_API bool validate_mnemonic(word_list const& mnemonic,
    dictionary_list const& lexicons=language::all);

/**
 * Convert a mnemonic with no passphrase to a wallet-generation seed.
 */
KI_API long_hash decode_mnemonic(word_list const& mnemonic);

/**
 * Convert a mnemonic and passphrase to a wallet-generation seed.
 * Any passphrase can be used and will change the resulting seed.
 * The passphrase has to be normalized using ICU.
 */
KI_API long_hash decode_mnemonic_normalized_passphrase(word_list const& mnemonic, std::string const& normalized_passphrase);

#ifdef WITH_ICU

/**
 * Convert a mnemonic and passphrase to a wallet-generation seed.
 * Any passphrase can be used and will change the resulting seed.
 */
KI_API long_hash decode_mnemonic(word_list const& mnemonic,
    std::string const& passphrase);

#endif

} // namespace kth::infrastructure::wallet

#endif
