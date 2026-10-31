// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>
#include <fstream>
#include <sstream>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace kth;

TEST_CASE("parser retains explicit settings after network defaults are rebuilt", "[configuration tests][parser]") {
    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--db.directory", "selected-database",
        "--rpc.enabled=true", "--net.outbound_connections", "0"};
    std::ostringstream error;
    REQUIRE(parser.parse(8, argv, error));
    CHECK(parser.configured.database.directory == path{"selected-database"});
    CHECK(parser.configured.rpc.enabled);
    CHECK(parser.configured.network.outbound_connections == 0);
}

namespace {

int process_id() {
#if defined(_WIN32)
    return ::_getpid();
#else
    return ::getpid();
#endif
}

// Writes a flat config file for the duration of a test.
struct config_file {
    explicit config_file(std::string const& tag, std::string const& text)
        : path_(std::filesystem::temp_directory_path() / ("kth_cfg_" + tag + "_" + std::to_string(process_id()) + ".cfg"))
    {
        std::ofstream(path_) << text << "\n";
    }
    ~config_file() { std::error_code ec; std::filesystem::remove(path_, ec); }
    std::filesystem::path const& path() const { return path_; }
private:
    std::filesystem::path path_;
};

} // namespace

namespace {

#if !defined(KTH_CURRENCY_BCH)
auto const mainnet_refusal = "acceptnonstdtxn is not allowed on mainnet";

bool contains(std::ostringstream const& error, char const* text) {
    return error.str().find(text) != std::string::npos;
}

#endif

} // namespace

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("Knuth standardness can be disabled on mainnet without disabling consensus", "[configuration tests][parser][standardness]") {
    node::parser parser(domain::config::network::mainnet);
    std::ostringstream error;
    char const* argv[]{"kth", "--network", "mainnet", "--node.acceptnonstdtxn", "true"};
    REQUIRE(parser.parse(5, argv, error));
    CHECK(parser.configured.chain.accept_non_standard_transactions);
    node::parser alias(domain::config::network::mainnet);
    char const* alternate[]{"kth", "--acceptnonstdtxn", "true"};
    REQUIRE(alias.parse(3, alternate, error));
    CHECK(alias.configured.chain.accept_non_standard_transactions);
    config_file file("mainnet_knuth_off", "node.acceptnonstdtxn=true");
    node::parser from_file(domain::config::network::mainnet);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.chain.accept_non_standard_transactions);
}
#endif

#if !defined(KTH_CURRENCY_BCH)
TEST_CASE("mainnet cannot accept non-standard transactions", "[configuration tests][parser][tx_size]") {
    {
        std::ostringstream error;
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--node.acceptnonstdtxn", "true"};
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(contains(error, mainnet_refusal));
    }
    {
        std::ostringstream error;
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--acceptnonstdtxn", "true"};
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(contains(error, mainnet_refusal));
    }
    {
        std::ostringstream error;
        config_file file("parse", "node.acceptnonstdtxn=true");
        auto const path = file.path().string();
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--config", path.c_str()};
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(contains(error, mainnet_refusal));
    }
    {
        std::ostringstream error;
        config_file file("from_file", "node.acceptnonstdtxn=true");
        node::parser parser(domain::config::network::mainnet);
        CHECK_FALSE(parser.parse_from_file(file.path(), error));
        CHECK(contains(error, mainnet_refusal));
    }
    {
        // Explicitly requiring standardness on mainnet is the default and allowed.
        std::ostringstream error;
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--node.acceptnonstdtxn", "false"};
        REQUIRE(parser.parse(5, argv, error));
        CHECK_FALSE(parser.configured.chain.accept_non_standard_transactions);
    }
}

#endif

TEST_CASE("acceptnonstdtxn defaults follow the reference node per network and can be toggled on test networks",
          "[configuration tests][parser][tx_size]") {
    auto const parse = [](char const* network, char const* value) {
        node::parser parser(domain::config::network::mainnet);
        std::ostringstream error;
        if (value) {
            char const* argv[]{"kth", "--network", network, "--node.acceptnonstdtxn", value};
            REQUIRE(parser.parse(5, argv, error));
        } else {
            char const* argv[]{"kth", "--network", network};
            REQUIRE(parser.parse(3, argv, error));
        }
        return parser.configured.chain.accept_non_standard_transactions;
    };
    CHECK_FALSE(parse("mainnet", nullptr));
    CHECK_FALSE(parse("regtest", nullptr));
    CHECK(parse("testnet", nullptr));
#if defined(KTH_CURRENCY_BCH)
    CHECK_FALSE(parse("testnet4", nullptr));
    CHECK_FALSE(parse("chipnet", nullptr));
    CHECK(parse("scalenet", nullptr));
    CHECK(parse("testnet4", "true"));
    CHECK_FALSE(parse("scalenet", "false"));
#endif
    CHECK(parse("regtest", "true"));
    CHECK_FALSE(parse("testnet", "false"));

    // Through a configuration file, with the network chosen on the command line.
    config_file file("toggle", "node.acceptnonstdtxn=true");
    auto const path = file.path().string();
    node::parser parser(domain::config::network::mainnet);
    std::ostringstream error;
    char const* argv[]{"kth", "--network", "regtest", "--config", path.c_str()};
    REQUIRE(parser.parse(5, argv, error));
    CHECK(parser.configured.chain.accept_non_standard_transactions);
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.chain.accept_non_standard_transactions);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("Knuth standardness parameters are configurable and initially match consensus", "[configuration tests][parser][standardness]") {
    node::parser defaults(domain::config::network::mainnet);
    CHECK(defaults.configured.chain.standardness.maximum_transaction_size == max_tx_size);
    CHECK(defaults.configured.chain.standardness.maximum_scriptsig_size == max_script_size);
    CHECK(defaults.configured.chain.standardness.minimum_output_value == 0);
    CHECK(defaults.configured.chain.minimum_output_satoshis == 0);
    node::parser configured(domain::config::network::mainnet);
    std::ostringstream error;
    char const* argv[]{"kth", "--node.standardness.maximum_transaction_size", "100000",
        "--node.standardness.maximum_scriptsig_size", "1650",
        "--node.standardness.minimum_output_value", "546"};
    REQUIRE(configured.parse(7, argv, error));
    CHECK(configured.configured.chain.standardness.maximum_transaction_size == 100000);
    CHECK(configured.configured.chain.standardness.maximum_scriptsig_size == 1650);
    CHECK(configured.configured.chain.standardness.minimum_output_value == 546);
}
#endif

TEST_CASE("the former size-only option is not accepted on the command line", "[configuration tests][parser][tx_size]") {
    for (auto network : {"mainnet", "regtest"}) {
        node::parser parser(domain::config::network::mainnet);
        std::ostringstream error;
        char const* argv[]{"kth", "--network", network, "--node.require_standard_size", "false"};
        CHECK_FALSE(parser.parse(5, argv, error));
    }
}

#if !defined(KTH_DB_READONLY)
TEST_CASE("offline compaction flag survives network selection and excludes initialization", "[configuration tests][parser]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--compact-utxo"};
    std::ostringstream error;
    REQUIRE(parser.parse(4, argv, error));
    CHECK(parser.configured.compact_utxo);
    node::parser conflicting(domain::config::network::mainnet);
    char const* invalid[]{"kth", "--compact-utxo", "--init_run"};
    CHECK_FALSE(conflicting.parse(3, invalid, error));
}
#endif

// Start Test Suite: configuration tests

// constructors
//-----------------------------------------------------------------------------

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("configuration construct1 testnet4 context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::testnet4);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 scalenet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::scalenet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 chipnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::chipnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}
#endif

TEST_CASE("configuration construct1 mainnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::mainnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

TEST_CASE("configuration construct1 testnet context expected", "[configuration tests]") {
    node::configuration instance(domain::config::network::testnet);
    REQUIRE( ! instance.help);
    REQUIRE( ! instance.initchain);
    REQUIRE( ! instance.settings);
    REQUIRE( ! instance.version);
    REQUIRE(instance.node.sync_peers == 0u);
    REQUIRE(instance.node.sync_timeout_seconds == 5u);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("configuration construct2 testnet4 context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::testnet4);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}

TEST_CASE("configuration construct2 scalenet context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::scalenet);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}

TEST_CASE("configuration construct2 chipnet context expected", "[configuration tests]") {
    node::configuration instance1(domain::config::network::chipnet);
    instance1.help = true;
    instance1.initchain = true;
    instance1.settings = true;
    instance1.version = true;
    instance1.node.sync_peers = 42;
    instance1.node.sync_timeout_seconds = 24;

    node::configuration instance2(instance1);

    REQUIRE(instance2.help);
    REQUIRE(instance2.initchain);
    REQUIRE(instance2.settings);
    REQUIRE(instance2.version);
    REQUIRE(instance2.node.sync_peers == 42u);
    REQUIRE(instance2.node.sync_timeout_seconds == 24u);
}
#endif

// End Test Suite

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("parser config network rebuilds defaults and preserves command line priority", "[configuration tests][parser][file_network]") {
    config_file file("selected_network", "network=chipnet\nchain.cores=3\nnet.inbound_port=12345\nfork.asert_half_life=1234\n");
    for (bool const override_network : {false, true}) {
        CAPTURE(override_network);
        node::parser parser(domain::config::network::mainnet);
        std::ostringstream errors;
        auto const path = file.path().string();
        char const* argv[]{"kth", "--config", path.c_str(), "--chain.cores", "7",
            "--network", "testnet4"};
        REQUIRE(parser.parse(override_network ? 7 : 5, argv, errors));
        auto const selected = override_network ? domain::config::network::testnet4
            : domain::config::network::chipnet;
        node::configuration const expected(selected);
        auto const& actual = parser.configured;
        CHECK(actual.net == selected);
        CHECK(actual.network.identifier == expected.network.identifier);
        CHECK(actual.network.hosts_file == expected.network.hosts_file);
        CHECK(actual.database.directory == expected.database.directory);
        CHECK(actual.chain.abla_config.epsilon0 == expected.chain.abla_config.epsilon0);
        CHECK(actual.chain.abla_config.beta0 == expected.chain.abla_config.beta0);
        CHECK(actual.chain.cantor_activation_time == expected.chain.cantor_activation_time);
        CHECK(actual.chain.checkpoints == expected.chain.checkpoints);
        CHECK(actual.chain.cores == 7);
        CHECK(actual.network.inbound_port == 12345);
        CHECK(actual.chain.asert_half_life == 1234);
    }
}
#endif
