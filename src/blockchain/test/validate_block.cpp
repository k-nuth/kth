// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/blockchain.hpp>

using namespace kth;
using namespace kd::chain;
using namespace kth::blockchain;
using namespace kd::machine;

// Start Test Suite: validate block tests

TEST_CASE("validate block native block 438513 tx valid", "[validate block tests]") {
    //// DEBUG [blockchain] Input validation failed (stack false)
    //// forks        : 62
    //// outpoint     : 8e51d775e0896e03149d585c0655b3001da0c55068b0885139ac6ec34cf76ba0:0
    //// script       : a914faa558780a5767f9e3be14992a578fc1cbcf483087
    //// inpoint      : 6b7f50afb8448c39f4714a73d2b181d3e3233e84670bdfda8f141db668226c54:0
    //// transaction  : 0100000001a06bf74cc36eac395188b06850c5a01d00b355065c589d14036e89e075d7518e000000009d483045022100ba555ac17a084e2a1b621c2171fa563bc4fb75cd5c0968153f44ba7203cb876f022036626f4579de16e3ad160df01f649ffb8dbf47b504ee56dc3ad7260af24ca0db0101004c50632102768e47607c52e581595711e27faffa7cb646b4f481fe269bd49691b2fbc12106ad6704355e2658b1756821028a5af8284a12848d69a25a0ac5cea20be905848eb645fd03d3b065df88a9117cacfeffffff0158920100000000001976a9149d86f66406d316d44d58cbf90d71179dd8162dd388ac355e2658

    static auto const index = 0u;
    static auto const forks = 62u;
    static auto const encoded_script = "a914faa558780a5767f9e3be14992a578fc1cbcf483087";
    static auto const encoded_tx = "0100000001a06bf74cc36eac395188b06850c5a01d00b355065c589d14036e89e075d7518e000000009d483045022100ba555ac17a084e2a1b621c2171fa563bc4fb75cd5c0968153f44ba7203cb876f022036626f4579de16e3ad160df01f649ffb8dbf47b504ee56dc3ad7260af24ca0db0101004c50632102768e47607c52e581595711e27faffa7cb646b4f481fe269bd49691b2fbc12106ad6704355e2658b1756821028a5af8284a12848d69a25a0ac5cea20be905848eb645fd03d3b065df88a9117cacfeffffff0158920100000000001976a9149d86f66406d316d44d58cbf90d71179dd8162dd388ac355e2658";

    auto const decoded_tx = decode_base16(encoded_tx);
    REQUIRE(decoded_tx);

    auto const decoded_script = decode_base16(encoded_script);
    REQUIRE(decoded_script);

    transaction tx;
    REQUIRE(kd::entity_from_data(tx, *decoded_tx));

    auto const& input = tx.inputs()[index];
    auto& prevout = input.previous_output().validation.cache;

    prevout.set_value(0);
    prevout.set_script(kd::create<script>(*decoded_script, false));

    REQUIRE(prevout.script().is_valid());

    auto const result = validate_input::verify_script(tx, index, forks);

    REQUIRE(result.first == error::success);

}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("validate block native block 520679 tx valid", "[validate block tests]") {
    //// DEBUG [blockchain] Input validation failed (stack false)
    //// forks        : 62 (?)
    //// outpoint     : dae852c88a00e95141cfe924ac6667a91af87431988d23eff268ea3509d6d83c:1
    //// script       : 76a9149c1093566aa0812e4ea55b5dc3d19a4223fa84d388ac
    //// inpoint      : ace8aaf0ea1c9996171fb2f73dff1f972d9c4b5b916534c32e0ae7f4a011116f:0
    //// transaction  : 01000000013cd8d60935ea68f2ef238d983174f81aa96766ac24e9cf4151e9008ac852e8da010000006a47304402206ccfd8739b2f98350d91ff7fec529f8bc085459b36cf26a22d95606737d4381002204429c60535745ef0b71c14bf0a9df565e8c87b934ee0b2766971cf5b15d085c04121020f123b05aadc865fd60d1513144f48f5d8de3403d3c3f00ce233d53329f10ccaffffffff0156998501000000001976a914bf4679910a2ba81b7f3f2ee03fc77847dc673b2288ac00000000

    static auto const index = 0u;
    static auto const encoded_script = "76a9149c1093566aa0812e4ea55b5dc3d19a4223fa84d388ac";
    static auto const encoded_tx = "01000000013cd8d60935ea68f2ef238d983174f81aa96766ac24e9cf4151e9008ac852e8da010000006a47304402206ccfd8739b2f98350d91ff7fec529f8bc085459b36cf26a22d95606737d4381002204429c60535745ef0b71c14bf0a9df565e8c87b934ee0b2766971cf5b15d085c04121020f123b05aadc865fd60d1513144f48f5d8de3403d3c3f00ce233d53329f10ccaffffffff0156998501000000001976a914bf4679910a2ba81b7f3f2ee03fc77847dc673b2288ac00000000";

    //This value after conversion its equal to the above code.
    uint32_t native_forks = domain::machine::rule_fork::bip16_rule;
    native_forks |= domain::machine::rule_fork::bip65_rule;
    native_forks |= domain::machine::rule_fork::bip66_rule;
    native_forks |= domain::machine::rule_fork::bip112_rule;
    native_forks |= domain::machine::rule_fork::bch_uahf;
    native_forks |= domain::machine::rule_fork::bch_daa_cw144;

    auto const decoded_tx = decode_base16(encoded_tx);
    REQUIRE(decoded_tx);

    auto const decoded_script = decode_base16(encoded_script);
    REQUIRE(decoded_script);

    transaction tx;
    REQUIRE(kd::entity_from_data(tx, *decoded_tx));

    auto const& input = tx.inputs()[index];
    auto& prevout = input.previous_output().validation.cache;

    prevout.set_value(25533210);
    prevout.set_script(kd::create<script>(*decoded_script, false));
    REQUIRE(prevout.script().is_valid());

    auto const result = validate_input::verify_script(tx, index, native_forks);
    REQUIRE(result.first == error::success);
}


TEST_CASE("validate block 2018NOV block 520679 tx valid", "[validate block tests]") {
    //// DEBUG [blockchain] Input validation failed (stack false)
    // forks        : 1073973119
    // outpoint     : 208fc2edc6fbf4c6cf7fb3ac0c7a1cb23f88fc3ddcced6423ad02d429acb2d07:0
    // script       : 76a9149a45c630ad1ddde200adbf048a929329220dd9a388ac
    // value        : 801932
    // inpoint      : 0375c73a1216188d4dfaed7508740ec1fd601b226de9845325f939348eead1fd:0
    // transaction  : 0100000001072dcb9a422dd03a42d6cedc3dfc883fb21c7a0cacb37fcfc6f4fbc6edc28f20000000006b48304502210099212bdccb2f12d26a1e6d859601bcd76ae3c8861261c6143923937200fa62a40220114e8003a90ffcb6ab3e05641b3daf64006d7bc4f959870f04efb01cad9aa4f3412102822d3e9a0bd0be3f4fab74c2ac9c85f4a0316b331bf92b3c3ef4484975c85e24ffffffff013c000c00000000001976a91463b302f02c2635a4054aa9b43995abbaa28c6f1088ac00000000


    static auto const index = 0u;
    static auto const encoded_script = "76a9149a45c630ad1ddde200adbf048a929329220dd9a388ac";
    static auto const encoded_tx = "0100000001072dcb9a422dd03a42d6cedc3dfc883fb21c7a0cacb37fcfc6f4fbc6edc28f20000000006b48304502210099212bdccb2f12d26a1e6d859601bcd76ae3c8861261c6143923937200fa62a40220114e8003a90ffcb6ab3e05641b3daf64006d7bc4f959870f04efb01cad9aa4f3412102822d3e9a0bd0be3f4fab74c2ac9c85f4a0316b331bf92b3c3ef4484975c85e24ffffffff013c000c00000000001976a91463b302f02c2635a4054aa9b43995abbaa28c6f1088ac00000000";
    static auto const value = 801932;


    //This value after conversion its equal to the above code.
    // static const uint32_t branches = 296831u;
    uint32_t native_forks = domain::machine::rule_fork::bip16_rule;
    native_forks |= domain::machine::rule_fork::bip65_rule;
    native_forks |= domain::machine::rule_fork::bip66_rule;
    native_forks |= domain::machine::rule_fork::bip112_rule;
    native_forks |= domain::machine::rule_fork::bch_uahf;
    native_forks |= domain::machine::rule_fork::bch_daa_cw144;
    native_forks |= domain::machine::rule_fork::bch_euclid;
    native_forks |= domain::machine::rule_fork::bch_pisano;
    // native_forks |= domain::machine::rule_fork::cash_segwit_recovery;

    auto const decoded_tx = decode_base16(encoded_tx);
    REQUIRE(decoded_tx);

    auto const decoded_script = decode_base16(encoded_script);
    REQUIRE(decoded_script);

    transaction tx;
    REQUIRE(kd::entity_from_data(tx, *decoded_tx));

    auto const& input = tx.inputs()[index];
    auto& prevout = input.previous_output().validation.cache;

    prevout.set_value(value);
    prevout.set_script(kd::create<script>(*decoded_script, false));
    REQUIRE(prevout.script().is_valid());

    auto const result = validate_input::verify_script(tx, index, native_forks);
    REQUIRE(result.first == error::success);
}
#endif

// End Test Suite
