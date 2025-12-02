// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/wallet/mnemonic.hpp>

#include "../math/external/pkcs5_pbkdf2.h"

#include <algorithm>
#include <cstdint>

#include <boost/locale.hpp>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/binary.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/string.hpp>
#include <kth/infrastructure/wallet/dictionary.hpp>

namespace kth::infrastructure::wallet {

// BIP-39 private constants.
static constexpr size_t bits_per_word = 11;
static constexpr size_t entropy_bit_divisor = 32;
static constexpr size_t hmac_iterations = 2048;
static char const* passphrase_prefix = "mnemonic";

inline
uint8_t bip39_shift(size_t bit) {
    return (1 << (byte_bits - (bit % byte_bits) - 1));
}

bool validate_mnemonic(const word_list& words, const dictionary& lexicon) {
    auto const word_count = words.size();
    if ((word_count % mnemonic_word_multiple) != 0) {
        return false;
    }

    auto const total_bits = bits_per_word * word_count;
    auto const check_bits = total_bits / (entropy_bit_divisor + 1);
    auto const entropy_bits = total_bits - check_bits;

    KTH_ASSERT((entropy_bits % byte_bits) == 0);

    size_t bit = 0;
    data_chunk data((total_bits + byte_bits - 1) / byte_bits, 0);

    for (auto const& word: words) {
        auto const position = find_position(lexicon, word);
        if (position == -1) {
            return false;
        }

        for (size_t loop = 0; loop < bits_per_word; loop++, bit++) {
            if ((position & (1 << (bits_per_word - loop - 1))) != 0) {
                auto const byte = bit / byte_bits;
                data[byte] |= bip39_shift(bit);
            }
        }
    }

    data.resize(entropy_bits / byte_bits);

    auto const mnemonic = create_mnemonic(data, lexicon);
    return std::equal(mnemonic.begin(), mnemonic.end(), words.begin());
}

word_list create_mnemonic(byte_span entropy, const dictionary &lexicon) {
    if ((entropy.size() % mnemonic_seed_multiple) != 0) {
        return word_list();
    }

    size_t const entropy_bits = (entropy.size() * byte_bits);
    size_t const check_bits = (entropy_bits / entropy_bit_divisor);
    size_t const total_bits = (entropy_bits + check_bits);
    size_t const word_count = (total_bits / bits_per_word);

    KTH_ASSERT((total_bits % bits_per_word) == 0);
    KTH_ASSERT((word_count % mnemonic_word_multiple) == 0);

    auto const data = build_chunk({entropy, sha256_hash(entropy)});

    size_t bit = 0;
    word_list words;

    for (size_t word = 0; word < word_count; word++) {
        size_t position = 0;
        for (size_t loop = 0; loop < bits_per_word; loop++) {
            bit = (word * bits_per_word + loop);
            position <<= 1;

            auto const byte = bit / byte_bits;

            if ((data[byte] & bip39_shift(bit)) > 0) {
                position++;
            }
        }

        KTH_ASSERT(position < dictionary_size);
        words.push_back(lexicon[position]);
    }

    KTH_ASSERT(words.size() == ((bit + 1) / bits_per_word));
    return words;
}

bool validate_mnemonic(const word_list& mnemonic, const dictionary_list& lexicons) {
    for (auto const& lexicon: lexicons) {
        if (validate_mnemonic(mnemonic, *lexicon)) {
            return true;
        }
    }

    return false;
}

long_hash decode_mnemonic(const word_list& mnemonic) {
    auto const sentence = join(mnemonic);
    std::string const salt(passphrase_prefix);
    return pkcs5_pbkdf2_hmac_sha512(to_chunk(sentence), to_chunk(salt), hmac_iterations);
}

long_hash decode_mnemonic_normalized_passphrase(const word_list& mnemonic, std::string const& normalized_passphrase) {
    auto const sentence = join(mnemonic);
    std::string const prefix(passphrase_prefix);
    auto const salt = prefix + normalized_passphrase;
    return pkcs5_pbkdf2_hmac_sha512(to_chunk(sentence), to_chunk(salt), hmac_iterations);
}


#ifdef WITH_ICU

long_hash decode_mnemonic(const word_list& mnemonic, std::string const& passphrase) {
    auto const sentence = join(mnemonic);
    std::string const prefix(passphrase_prefix);
    auto const salt = to_normal_nfkd_form(prefix + passphrase);
    return pkcs5_pbkdf2_hmac_sha512(to_chunk(sentence), to_chunk(salt), hmac_iterations);
}

#endif

} // namespace kth::infrastructure::wallet
