// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mnemonic.hpp"

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure::wallet;

// Start Test Suite: mnemonic tests

TEST_CASE("infrastructure mnemonic decode without passphrase", "[infrastructure][mnemonic]") {
    for (auto const& vector: mnemonic_no_passphrase) {
        auto const words = split(vector.mnemonic, ",");
        REQUIRE(validate_mnemonic(words, vector.language));
        auto const seed = decode_mnemonic(words);
        REQUIRE(encode_base16(seed) == vector.seed);
    }
}

#ifdef WITH_ICU

TEST_CASE("infrastructure mnemonic decode with trezor test vectors", "[infrastructure][mnemonic]") {
    for (auto const& vector: mnemonic_trezor_vectors) {
        auto const words = split(vector.mnemonic, ",");
        REQUIRE(validate_mnemonic(words));
        auto const seed = decode_mnemonic(words, vector.passphrase);
        REQUIRE(encode_base16(seed) == vector.seed);
    }
}

TEST_CASE("infrastructure mnemonic decode with bx test vectors", "[infrastructure][mnemonic]") {
    for (auto const& vector: mnemonic_bx_to_seed_vectors) {
        auto const words = split(vector.mnemonic, ",");
        REQUIRE(validate_mnemonic(words));
        auto const seed = decode_mnemonic(words, vector.passphrase);
        REQUIRE(encode_base16(seed) == vector.seed);
    }
}

#endif

TEST_CASE("infrastructure mnemonic create from entropy trezor vectors", "[infrastructure][mnemonic]") {
    for (mnemonic_result const& vector : mnemonic_trezor_vectors) {
        data_chunk entropy;
        decode_base16(entropy, vector.entropy);
        auto const mnemonic = create_mnemonic(entropy, vector.language);
        REQUIRE(mnemonic.size() > 0);
        REQUIRE(join(mnemonic, ",") == vector.mnemonic);
        REQUIRE(validate_mnemonic(mnemonic));
    }
}

TEST_CASE("mnemonic  create mnemonic  bx", "[mnemonic tests]") {
    for (const mnemonic_result& vector: mnemonic_bx_new_vectors) {
        data_chunk entropy;
        decode_base16(entropy, vector.entropy);
        auto const mnemonic = create_mnemonic(entropy, vector.language);
        REQUIRE(mnemonic.size() > 0);
        REQUIRE(join(mnemonic, ",") == vector.mnemonic);
        REQUIRE(validate_mnemonic(mnemonic));
    }
}

TEST_CASE("mnemonic  validate mnemonic  invalid", "[mnemonic tests]") {
    for (auto const& mnemonic: invalid_mnemonic_tests) {
        auto const words = split(mnemonic, ",");
        REQUIRE( ! validate_mnemonic(words));
    }
}

TEST_CASE("mnemonic  create mnemonic  tiny", "[mnemonic tests]") {
    data_chunk const entropy(4, 0xa9);
    auto const mnemonic = create_mnemonic(entropy);
    REQUIRE(mnemonic.size() == 3u);
    REQUIRE(validate_mnemonic(mnemonic));
}

TEST_CASE("mnemonic  create mnemonic  giant", "[mnemonic tests]") {
    data_chunk const entropy(1024, 0xa9);
    auto const mnemonic = create_mnemonic(entropy);
    REQUIRE(mnemonic.size() == 768u);
    REQUIRE(validate_mnemonic(mnemonic));
}

TEST_CASE("mnemonic  dictionary  en es  no intersection", "[mnemonic tests]") {
    auto const& english = language::en;
    auto const& spanish = language::es;
    size_t intersection = 0;
    for (auto const es: spanish) {
        std::string test(es);
        auto const it = std::find(english.begin(), english.end(), test);
        if (it != std::end(english))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  en it  no intersection", "[mnemonic tests]") {
    auto const& english = language::en;
    auto const& italian = language::it;
    size_t intersection = 0;
    for (auto const it: italian) {
        std::string test(it);
        auto const iter = std::find(english.begin(), english.end(), test);
        if (iter != std::end(english))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  fr es  no intersection", "[mnemonic tests]") {
    auto const& french = language::fr;
    auto const& spanish = language::es;
    size_t intersection = 0;
    for (auto const es: spanish) {
        std::string test(es);
        auto const it = std::find(french.begin(), french.end(), test);
        if (it != std::end(french))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  it es  no intersection", "[mnemonic tests]") {
    auto const& italian = language::it;
    auto const& spanish = language::es;
    size_t intersection = 0;
    for (auto const es: spanish) {
        std::string test(es);
        auto const it = std::find(italian.begin(), italian.end(), test);
        if (it != std::end(italian))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  fr it  no intersection", "[mnemonic tests]") {
    auto const& french = language::fr;
    auto const& italian = language::it;
    size_t intersection = 0;
    for (auto const it: italian) {
        std::string test(it);
        auto const iter = std::find(french.begin(), french.end(), test);
        if (iter != std::end(french))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  cs ru  no intersection", "[mnemonic tests]") {
    auto const& czech = language::cs;
    auto const& russian = language::ru;
    size_t intersection = 0;
    for (auto const ru: russian) {
        std::string test(ru);
        auto const iter = std::find(czech.begin(), czech.end(), test);
        if (iter != std::end(czech))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  cs uk  no intersection", "[mnemonic tests]") {
    auto const& czech = language::cs;
    auto const& ukranian = language::uk;
    size_t intersection = 0;
    for (auto const uk: ukranian) {
        std::string test(uk);
        auto const iter = std::find(czech.begin(), czech.end(), test);
        if (iter != std::end(czech))
            intersection++;
    }

    REQUIRE(intersection == 0u);
}

TEST_CASE("mnemonic  dictionary  zh Hans Hant  intersection", "[mnemonic tests]") {
    auto const& simplified = language::zh_Hans;
    auto const& traditional = language::zh_Hant;
    size_t intersection = 0;
    for (auto const hant: traditional) {
        std::string test(hant);
        auto const it = std::find(simplified.begin(), simplified.end(), test);
        if (it != std::end(simplified))
            intersection++;
    }

    REQUIRE(intersection == 1275u);
}

// End Test Suite
