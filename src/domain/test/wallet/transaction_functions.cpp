// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <kth/domain/wallet/transaction_functions.hpp>
#include <test_helpers.hpp>
#include <kth/infrastructure/wallet/hd_private.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;
using namespace kth::infrastructure::wallet;

// Start Test Suite: transaction functions tests

constexpr char seed[] = "fffb587496cc54912bbcef874fa9a61a";
constexpr char wallet[] = "mwx2YDHgpdfHUmCpFjEi9LarXf7EkQN6YG";
constexpr char tx_encoded[] = "01000000019373b022dfb99400ee40b8987586aea9e158f3b0c62343d59896c212cee60d980100000000ffffffff0118beeb0b000000001976a914b43ff4532569a00bcab4ce60f87cdeebf985b69a88ac00000000";
// constexpr char signature[] = "30440220433c405e4cb7698ad5f58e0ea162c3c3571d46d96ff1b3cb9232a06eba3b444d02204bc5f48647c0f052ade7cf85eac3911f7afbfa69fa5ebd92084191a5da33f88d41";
// constexpr char complete_tx[] = "01000000019373b022dfb99400ee40b8987586aea9e158f3b0c62343d59896c212cee60d98010000006a4730440220433c405e4cb7698ad5f58e0ea162c3c3571d46d96ff1b3cb9232a06eba3b444d02204bc5f48647c0f052ade7cf85eac3911f7afbfa69fa5ebd92084191a5da33f88d4121027a45d4abb6ebb00214796e2c7cf61d18c9185ba771fe9ed75b303eb7a8e9028bffffffff0118beeb0b000000001976a914b43ff4532569a00bcab4ce60f87cdeebf985b69a88ac00000000";
constexpr char signature[] = "304402200f9a99b998eb71db23e179226c8e2068b6fb1dc58ea428ef75b7e07acf8a2f3c02201398ee98bffd4a9f16abc0513131bc67f9b4e432bd80a0b48e5b6259dd3cb27341";
constexpr char complete_tx[] = "01000000019373b022dfb99400ee40b8987586aea9e158f3b0c62343d59896c212cee60d98010000006a47304402200f9a99b998eb71db23e179226c8e2068b6fb1dc58ea428ef75b7e07acf8a2f3c02201398ee98bffd4a9f16abc0513131bc67f9b4e432bd80a0b48e5b6259dd3cb2734121027a45d4abb6ebb00214796e2c7cf61d18c9185ba771fe9ed75b303eb7a8e9028bffffffff0118beeb0b000000001976a914b43ff4532569a00bcab4ce60f87cdeebf985b69a88ac00000000";

// Helpers to replicate the kth-dojo functionality

kth::ec_secret create_secret_from_seed(std::string const& seed_str) {
    auto seed = kth::decode_base16(seed_str);
    hd_private const key(*seed);
    // Secret key
    kth::ec_secret secret_key(key.secret());
    return secret_key;
}

ec_public secret_to_compressed_public(kth::ec_secret const& secret_key) {
    //Public key
    kth::ec_compressed point;
    kth::secret_to_public(point, secret_key);
    ec_public public_key(point, true /*compress*/);

    return public_key;
}

TEST_CASE("seed to wallet compressed test", "[transaction functions]") {
    auto secret = create_secret_from_seed(seed);
    auto pub_key = secret_to_compressed_public(secret);
    // Payment Address
    uint8_t const version = payment_address::testnet_p2kh;  // testnet_p2sh
    payment_address address(pub_key, version);

    REQUIRE(address.encoded() == wallet);
}

TEST_CASE("create transaction test", "[transaction functions]") {
    // List of inputs (outputs_to_spend)
    std::vector<chain::input_point> outputs_to_spend;
    kth::hash_digest hash_to_spend;
    kth::decode_hash(hash_to_spend, "980de6ce12c29698d54323c6b0f358e1a9ae867598b840ee0094b9df22b07393");
    uint32_t const index_to_spend = 1;
    outputs_to_spend.push_back({hash_to_spend, index_to_spend});

    // List of outputs
    std::vector<std::pair<payment_address, uint64_t>> outputs;
    outputs.push_back({payment_address(wallet), 199999000});

    auto result = tx_encode(outputs_to_spend, outputs);

    REQUIRE(result.first == error::error_code_t::success);
    REQUIRE(kth::encode_base16(result.second.to_data()) == tx_encoded);
}

// TODO(legacy): make test for BTC and LTC signatures

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("sign transaction test", "[transaction functions]") {
    // Priv key
    auto const private_key = create_secret_from_seed(seed);
    // Script
    auto raw_script = kth::decode_base16("76a914b43ff4532569a00bcab4ce60f87cdeebf985b69a88ac");
    REQUIRE(raw_script);
    chain::script output_script(*raw_script, false);
    // TX
    chain::transaction tx;
    auto raw_data = kth::decode_base16(tx_encoded);
    REQUIRE(raw_data);
    entity_from_data(tx, *raw_data);
    REQUIRE(kth::encode_base16(tx.to_data()) == tx_encoded);
    // Amount
    uint64_t const amount = 200000000;
    // Index
    int const index = 0;
    // Create signature
    auto const result = input_signature_bch(private_key, output_script, tx, amount, index);

    REQUIRE(result.first == error::error_code_t::success);
    REQUIRE(kth::encode_base16(result.second) == signature);
}
#endif

TEST_CASE("set signature test", "[transaction functions]") {
    // TX
    chain::transaction tx;
    auto raw_data = kth::decode_base16(tx_encoded);
    REQUIRE(raw_data);
    entity_from_data(tx, *raw_data);

    // SCRIPT
    auto const secret_key = create_secret_from_seed(seed);
    auto pub_key = secret_to_compressed_public(secret_key);
    // Redeem script for P2KH [SIGNATURE][PUBKEY]
    chain::script input_script;
    input_script.from_string("[" + std::string(signature) + "] [" + pub_key.encoded() + "]");

    // SET THE INPUT
    auto const result = input_set(input_script, tx);
    REQUIRE(result.first == error::error_code_t::success);
    REQUIRE(kth::encode_base16(result.second.to_data()) == complete_tx);
}

// End Test Suite
