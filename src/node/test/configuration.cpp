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

TEST_CASE("sv2 listener settings survive network selection and config loading", "[configuration tests][parser][sv2 template_provider]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled=true",
        "--sv2.bind", "::1", "--sv2.port", "18442"};
    std::ostringstream error;
    REQUIRE(parser.parse(8, argv, error));
    CHECK(parser.configured.sv2.enabled);
    CHECK(parser.configured.sv2.bind == "::1");
    CHECK(parser.configured.sv2.port == 18442u);

    config_file file("sv2", "sv2.enabled=true\nsv2.bind=127.0.0.2\nsv2.port=0");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.sv2.enabled);
    CHECK(from_file.configured.sv2.bind == "127.0.0.2");
    CHECK(from_file.configured.sv2.port == 0u);

    node::configuration const copied(from_file.configured);
    CHECK(copied.sv2.enabled);
    CHECK(copied.sv2.bind == "127.0.0.2");
    CHECK(copied.sv2.port == 0u);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    char const* override_argv[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.enabled=false", "--sv2.bind", "127.0.0.1", "--sv2.port", "8442"};
    REQUIRE(overridden.parse(10, override_argv, error));
    CHECK_FALSE(overridden.configured.sv2.enabled);
    CHECK(overridden.configured.sv2.bind == "127.0.0.1");
    CHECK(overridden.configured.sv2.port == 8442u);
}

TEST_CASE("sv2 listener settings reject invalid ports", "[configuration tests][parser][sv2 template_provider]") {
    for (auto const* port : {"-1", "65536", "invalid"}) {
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        char const* argv[]{"kth", "--sv2.port", port};
        CHECK_FALSE(parser.parse(3, argv, error));
    }
}

TEST_CASE("sv2 mining settings survive network selection config and overrides", "[configuration tests][parser][sv2 mining]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled=true",
        "--sv2.mining.enabled=true", "--sv2.mining.address", "bchreg:payout"};
    std::ostringstream error;
    REQUIRE(parser.parse(7, argv, error));
    CHECK(parser.configured.sv2.enabled);
    CHECK(parser.configured.sv2.mining.enabled);
    CHECK(parser.configured.sv2.mining.address == "bchreg:payout");

    config_file file("sv2_mining", "sv2.enabled=true\nsv2.mining.enabled=true\n"
        "sv2.mining.address=bchreg:configured\nsv2.mining.open_channel_timeout_seconds=30");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.sv2.mining.enabled);
    CHECK(from_file.configured.sv2.mining.address == "bchreg:configured");
    CHECK(from_file.configured.sv2.mining.open_channel_timeout_seconds == 30u);
    node::configuration const copied(from_file.configured);
    CHECK(copied.sv2.mining.enabled);
    CHECK(copied.sv2.mining.address == "bchreg:configured");
    CHECK(copied.sv2.mining.open_channel_timeout_seconds == 30u);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    char const* override_argv[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.mining.enabled=false", "--sv2.mining.address", "bchreg:selected",
        "--sv2.mining.open_channel_timeout_seconds", "45"};
    REQUIRE(overridden.parse(10, override_argv, error));
    CHECK_FALSE(overridden.configured.sv2.mining.enabled);
    CHECK(overridden.configured.sv2.mining.address == "bchreg:selected");
    CHECK(overridden.configured.sv2.mining.open_channel_timeout_seconds == 45u);
}

TEST_CASE("sv2 mining enable option rejects an invalid boolean", "[configuration tests][parser][sv2 mining]") {
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    char const* argv[]{"kth", "--sv2.mining.enabled=invalid"};
    CHECK_FALSE(parser.parse(2, argv, error));
}

TEST_CASE("sv2 JD settings survive network selection config copying and command line overrides", "[configuration tests][parser][sv2 jd]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.jd.enabled=true",
        "--sv2.jd.mining_address", "pool.example:3333",
        "--sv2.jd.mining_authority_public_key", "pool-pin",
        "--sv2.jd.jds_address", "[::1]:3334",
        "--sv2.jd.jds_authority_public_key", "jds-pin",
        "--sv2.jd.user_identity", "account.worker", "--sv2.jd.mode", "full-template",
        "--sv2.jd.response_timeout_seconds", "12"};
    std::ostringstream error;
    REQUIRE(parser.parse(18, argv, error));
    auto const& selected = parser.configured.sv2.jd;
    CHECK(selected.enabled);
    CHECK(selected.mining_address == "pool.example:3333");
    CHECK(selected.mining_authority_public_key == "pool-pin");
    CHECK(selected.jds_address == "[::1]:3334");
    CHECK(selected.jds_authority_public_key == "jds-pin");
    CHECK(selected.user_identity == "account.worker");
    CHECK(selected.mode == "full-template");
    CHECK(selected.response_timeout_seconds == 12u);

    config_file file("sv2_jd", "sv2.jd.enabled=true\nsv2.jd.mining_address=pool.example:4444\n"
        "sv2.jd.mining_authority_public_key=configured-pool-pin\nsv2.jd.jds_address=jds.example:4445\n"
        "sv2.jd.jds_authority_public_key=configured-jds-pin\nsv2.jd.user_identity=configured-worker\n"
        "sv2.jd.mode=full-template\nsv2.jd.response_timeout_seconds=19");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    node::configuration const copied(from_file.configured);
    CHECK(copied.sv2.jd.enabled);
    CHECK(copied.sv2.jd.mining_address == "pool.example:4444");
    CHECK(copied.sv2.jd.mining_authority_public_key == "configured-pool-pin");
    CHECK(copied.sv2.jd.jds_address == "jds.example:4445");
    CHECK(copied.sv2.jd.jds_authority_public_key == "configured-jds-pin");
    CHECK(copied.sv2.jd.user_identity == "configured-worker");
    CHECK(copied.sv2.jd.mode == "full-template");
    CHECK(copied.sv2.jd.response_timeout_seconds == 19u);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    char const* overrides[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.jd.enabled=false", "--sv2.jd.mode", "coinbase-only",
        "--sv2.jd.user_identity", "selected-worker"};
    REQUIRE(overridden.parse(10, overrides, error));
    CHECK_FALSE(overridden.configured.sv2.jd.enabled);
    CHECK(overridden.configured.sv2.jd.mode == "coinbase-only");
    CHECK(overridden.configured.sv2.jd.user_identity == "selected-worker");
    CHECK(overridden.configured.sv2.jd.mining_address == "pool.example:4444");
    CHECK(overridden.configured.sv2.jd.response_timeout_seconds == 19u);

    node::parser defaulted(domain::config::network::mainnet);
    char const* enabled_only[]{"kth", "--network", "regtest", "--sv2.jd.enabled=true"};
    REQUIRE(defaulted.parse(4, enabled_only, error));
    CHECK(defaulted.configured.sv2.jd.mode == "coinbase-only");
    CHECK_FALSE(defaulted.configured.sv2.jd.own_upstream);
}

TEST_CASE("sv2 JD own_upstream reads from the command line and the config file", "[configuration tests][parser][sv2 jd]") {
    std::ostringstream error;
    node::parser marked(domain::config::network::regtest);
    char const* argv[]{"kth", "--sv2.jd.own_upstream=true"};
    REQUIRE(marked.parse(2, argv, error));
    CHECK(marked.configured.sv2.jd.own_upstream);

    config_file file("sv2_jd_own", "sv2.jd.own_upstream=true");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    CHECK(from_file.configured.sv2.jd.own_upstream);

    node::parser invalid(domain::config::network::regtest);
    char const* bad[]{"kth", "--sv2.jd.own_upstream=invalid"};
    CHECK_FALSE(invalid.parse(2, bad, error));
}

TEST_CASE("sv2 JD parser refuses malformed enable and deadline values", "[configuration tests][parser][sv2 jd]") {
    node::parser enabled(domain::config::network::regtest);
    std::ostringstream error;
    char const* invalid_bool[]{"kth", "--sv2.jd.enabled=invalid"};
    CHECK_FALSE(enabled.parse(2, invalid_bool, error));
    for (auto const* value : {"-1", "4294967296", "invalid"}) {
        node::parser timeout(domain::config::network::regtest);
        char const* argv[]{"kth", "--sv2.jd.response_timeout_seconds", value};
        CHECK_FALSE(timeout.parse(3, argv, error));
    }
}

TEST_CASE("sv2 JD failover parser preserves typed priority config copying and CLI overrides", "[configuration tests][parser][sv2 jd]") {
    std::string const first = "z-pool.example:3333|pool-key|[::1]:3334|jds-key|first.worker|full-template";
    std::string const second = "a-pool.example:4444|backup-key|jds.example:4445|backup-jds-key|second.worker|coinbase-only";
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--sv2.jd.upstream", first.c_str(),
        "--sv2.jd.upstream", second.c_str(), "--sv2.jd.retry_initial_seconds", "2",
        "--sv2.jd.retry_max_seconds", "60", "--sv2.jd.authority_hold_seconds", "300",
        "--sv2.jd.recovery_hold_seconds", "3", "--sv2.jd.probe_interval_seconds", "4",
        "--sv2.jd.max_latency_milliseconds", "250", "--sv2.jd.max_stale_percent", "25",
        "--sv2.jd.max_reject_percent", "15", "--sv2.jd.health_window", "40"};
    std::ostringstream error;
    REQUIRE(parser.parse(static_cast<int>(sizeof(argv) / sizeof(argv[0])), argv, error));
    auto const& selected = parser.configured.sv2.jd;
    REQUIRE(selected.upstreams.size() == 2);
    CHECK(selected.upstreams[0] == first);
    CHECK(selected.upstreams[1] == second);
    CHECK(selected.retry_initial_seconds == 2);
    CHECK(selected.retry_max_seconds == 60);
    CHECK(selected.authority_hold_seconds == 300);
    CHECK(selected.recovery_hold_seconds == 3);
    CHECK(selected.probe_interval_seconds == 4);
    CHECK(selected.max_latency_milliseconds == 250);
    CHECK(selected.max_stale_percent == 25);
    CHECK(selected.max_reject_percent == 15);
    CHECK(selected.health_window == 40);

    config_file file("sv2_jd_failover", "sv2.jd.upstream=\"" + first + "\"\nsv2.jd.retry_initial_seconds=7\n"
        "sv2.jd.upstream=\"" + second + "\"\nsv2.jd.retry_max_seconds=70\n"
        "sv2.jd.authority_hold_seconds=600\nsv2.jd.recovery_hold_seconds=9\n"
        "sv2.jd.probe_interval_seconds=8\nsv2.jd.max_latency_milliseconds=900\n"
        "sv2.jd.max_stale_percent=30\nsv2.jd.max_reject_percent=40\nsv2.jd.health_window=50");
    node::parser from_file(domain::config::network::regtest);
    REQUIRE(from_file.parse_from_file(file.path(), error));
    node::configuration const copied(from_file.configured);
    CHECK(copied.sv2.jd.upstreams == selected.upstreams);
    CHECK(copied.sv2.jd.retry_initial_seconds == 7);
    CHECK(copied.sv2.jd.retry_max_seconds == 70);
    CHECK(copied.sv2.jd.authority_hold_seconds == 600);
    CHECK(copied.sv2.jd.recovery_hold_seconds == 9);
    CHECK(copied.sv2.jd.probe_interval_seconds == 8);
    CHECK(copied.sv2.jd.max_latency_milliseconds == 900);
    CHECK(copied.sv2.jd.max_stale_percent == 30);
    CHECK(copied.sv2.jd.max_reject_percent == 40);
    CHECK(copied.sv2.jd.health_window == 50);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    char const* overrides[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.jd.upstream", second.c_str(), "--sv2.jd.upstream", first.c_str(),
        "--sv2.jd.health_window", "12"};
    REQUIRE(overridden.parse(11, overrides, error));
    REQUIRE(overridden.configured.sv2.jd.upstreams.size() == 2);
    CHECK(overridden.configured.sv2.jd.upstreams[0] == second);
    CHECK(overridden.configured.sv2.jd.upstreams[1] == first);
    CHECK(overridden.configured.sv2.jd.health_window == 12);
    CHECK(overridden.configured.sv2.jd.retry_initial_seconds == 7);

    config_file array_file("sv2_jd_failover_array", "sv2.jd.upstream=[\"" + second
        + "\", \"" + first + "\"]");
    node::parser array_parser(domain::config::network::regtest);
    REQUIRE(array_parser.parse_from_file(array_file.path(), error));
    CHECK(array_parser.configured.sv2.jd.upstreams == overridden.configured.sv2.jd.upstreams);

    config_file duplicate_scalar("sv2_jd_duplicate_scalar",
        "sv2.jd.health_window=20\nsv2.jd.health_window=30");
    node::parser strict_parser(domain::config::network::regtest);
    CHECK_FALSE(strict_parser.parse_from_file(duplicate_scalar.path(), error));
}

TEST_CASE("sv2 JD failover parser refuses malformed unsigned thresholds", "[configuration tests][parser][sv2 jd]") {
    for (auto const* option : {"--sv2.jd.retry_initial_seconds", "--sv2.jd.retry_max_seconds",
        "--sv2.jd.authority_hold_seconds", "--sv2.jd.recovery_hold_seconds", "--sv2.jd.probe_interval_seconds",
        "--sv2.jd.max_latency_milliseconds", "--sv2.jd.max_stale_percent", "--sv2.jd.max_reject_percent",
        "--sv2.jd.health_window"}) {
        INFO(option);
        for (auto const* value : {"-1", "4294967296", "invalid"}) {
            node::parser parser(domain::config::network::regtest);
            std::ostringstream error;
            char const* argv[]{"kth", option, value};
            CHECK_FALSE(parser.parse(3, argv, error));
        }
    }
}

TEST_CASE("sv2 Noise credentials survive config and command line parsing", "[configuration tests][parser][sv2 noise]") {
    config_file file("sv2_noise", "sv2.noise=false\nsv2.static_key_file=provider.key\n"
        "sv2.authority_key_file=authority.key\nsv2.certificate_file=provider.cert\n"
        "sv2.authority_public_key=pinned\nsv2.certificate_validity_seconds=600\n"
        "sv2.handshake_timeout_seconds=3");
    node::parser parser(domain::config::network::regtest);
    std::ostringstream error;
    REQUIRE(parser.parse_from_file(file.path(), error));
    auto const& settings = parser.configured.sv2;
    CHECK_FALSE(settings.noise);
    CHECK(settings.static_key_file == "provider.key");
    CHECK(settings.authority_key_file == "authority.key");
    CHECK(settings.certificate_file == "provider.cert");
    CHECK(settings.authority_public_key == "pinned");
    CHECK(settings.certificate_validity_seconds == 600u);
    CHECK(settings.handshake_timeout_seconds == 3u);

    auto const path = file.path().string();
    node::parser overridden(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "regtest", "--config", path.c_str(),
        "--sv2.noise=true", "--sv2.static_key_file", "selected.key",
        "--sv2.authority_key_file", "selected-authority.key",
        "--sv2.certificate_file", "selected.cert", "--sv2.authority_public_key", "selected-pin",
        "--sv2.certificate_validity_seconds", "900", "--sv2.handshake_timeout_seconds", "4"};
    REQUIRE(overridden.parse(18, argv, error));
    auto const& selected = overridden.configured.sv2;
    CHECK(selected.noise);
    CHECK(selected.static_key_file == "selected.key");
    CHECK(selected.authority_key_file == "selected-authority.key");
    CHECK(selected.certificate_file == "selected.cert");
    CHECK(selected.authority_public_key == "selected-pin");
    CHECK(selected.certificate_validity_seconds == 900u);
    CHECK(selected.handshake_timeout_seconds == 4u);
}

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
TEST_CASE("tempnet selection preserves its fork rules and isolated data directory", "[configuration tests][parser][tempnet]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "tempnet"};
    std::ostringstream error;
    REQUIRE(parser.parse(3, argv, error));
    auto const& cfg = parser.configured;
    CHECK(domain::config::chain_name(cfg.net, domain::config::chain_name_format::display) == "Tempnet");
    CHECK(cfg.chain.cantor_activation_time == 1792065600ULL);
    CHECK(cfg.network.identifier == 0xafdab7e2U);
    CHECK(cfg.network.inbound_port == 48333);
    CHECK(cfg.database.directory == path{"blockchain-tempnet"});
    CHECK(cfg.chain.easy_blocks);
    CHECK(cfg.chain.asert_half_life == 3600);
    CHECK(cfg.chain.default_consensus_block_size == 2'000'000);
    CHECK(cfg.chain.abla_config.epsilon_max > cfg.chain.abla_config.epsilon0);
}

TEST_CASE("tempnet selection retains an explicit database directory", "[configuration tests][parser][tempnet]") {
    node::parser parser(domain::config::network::mainnet);
    char const* argv[]{"kth", "--network", "tempnet", "--db.directory", "custom-tempnet"};
    std::ostringstream error;
    REQUIRE(parser.parse(5, argv, error));
    CHECK(parser.configured.database.directory == path{"custom-tempnet"});
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("tempnet isolation preserves other networks' default directories", "[configuration tests][tempnet][directories]") {
    using domain::config::network;
    CHECK(database::settings(network::tempnet).directory == path{"blockchain-tempnet"});
    CHECK(database::settings(network::testnet4).directory == path{"blockchain"});
    CHECK(database::settings(network::scalenet).directory == path{"blockchain"});
    CHECK(database::settings(network::chipnet).directory == path{"blockchain"});
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("parser tempnet config rebuilds defaults and preserves command line priority", "[configuration tests][parser][file_network]") {
    config_file file("selected_tempnet_network", "network=tempnet\nchain.cores=3\nnet.inbound_port=12345\nfork.asert_half_life=1234\n");
    for (bool const override_network : {false, true}) {
        CAPTURE(override_network);
        node::parser parser(domain::config::network::mainnet);
        std::ostringstream errors;
        auto const path = file.path().string();
        char const* argv[]{"kth", "--config", path.c_str(), "--chain.cores", "7",
            "--network", "chipnet"};
        REQUIRE(parser.parse(override_network ? 7 : 5, argv, errors));
        auto const selected = override_network ? domain::config::network::chipnet
            : domain::config::network::tempnet;
        node::configuration const expected(selected);
        auto const& actual = parser.configured;
        CHECK(actual.net == selected);
        CHECK(actual.network.context == selected);
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
