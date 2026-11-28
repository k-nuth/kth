// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/node.hpp>
#include <filesystem>
#include <fstream>
#include <future>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <sstream>
#include <algorithm>
#include <memory>
#include <string_view>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>
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

TEST_CASE("parser accepts a pruning depth of zero or at least the minimum", "[configuration tests][parser][prune]") {
    auto const parse = [](char const* value, uint32_t* parsed = nullptr) {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--db.prune_keep_blocks", value};
        std::ostringstream error;
        auto const ok = parser.parse(5, argv, error);
        if (ok && parsed) *parsed = parser.configured.database.prune_keep_blocks;
        return ok;
    };

    // Every block is kept unless the option says otherwise.
    {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest"};
        std::ostringstream error;
        REQUIRE(parser.parse(3, argv, error));
        CHECK(parser.configured.database.prune_keep_blocks == 0u);
    }

    uint32_t parsed = 1;
    CHECK(parse("0", &parsed));
    CHECK(parsed == 0u);
    CHECK(parse("288", &parsed));
    CHECK(parsed == 288u);
    CHECK(parse("10000", &parsed));
    CHECK(parsed == 10000u);

    // Fewer than a limited node has to serve.
    CHECK_FALSE(parse("1"));
    CHECK_FALSE(parse("287"));
    CHECK_FALSE(parse("-1"));
    CHECK_FALSE(parse("many"));
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

#if ! defined(KTH_WITH_SV2)
// A build without the feature has no Template Provider, so it does not start
// as if it listened.
TEST_CASE("the sv2 listener is off, and refused, in a build without the feature", "[configuration tests][parser][sv2]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK_FALSE(defaults.configured.sv2.enabled);

    SECTION("on the command line") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled", "true"};
        std::ostringstream error;
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(error.str().find("sv2.enabled needs a build with the option sv2") != std::string::npos);
    }
    SECTION("in the configuration file") {
        config_file file("sv2_refused", "sv2.enabled=true");
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        CHECK_FALSE(parser.parse_from_file(file.path(), error));
        CHECK(error.str().find("sv2.enabled needs a build with the option sv2") != std::string::npos);
    }
    SECTION("turned off, it is what the build does anyway") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--sv2.enabled", "false",
            "--sv2.bind", "::1", "--sv2.port", "18442"};
        std::ostringstream error;
        CHECK(parser.parse(9, argv, error));
    }
}

TEST_CASE("a node built without Stratum V2 does not start as one that listens for it", "[configuration tests][node][sv2]") {
    auto const dir = std::filesystem::temp_directory_path() / ("kth_sv2_node_refused_" + std::to_string(process_id()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = dir;
    cfg.database.db_max_size = 16ULL << 20;
    cfg.network.threads = 0;
    cfg.network.peers_file = dir / "peers.dat";
    cfg.network.hosts_file = dir / "hosts.dat";
    cfg.sv2.enabled = true;

    // The directory holds no database, so a start that got as far as opening
    // one fails too, with the same code. What the node wrote tells the two
    // apart: it refuses for the setting, before it opens anything.
    auto const sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    auto const previous = spdlog::default_logger();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("captured", sink));
    code result;
    {
        node::full_node node{cfg};
        ::asio::io_context io;
        auto started = ::asio::co_spawn(io, node.start(), ::asio::use_future);
        io.run();
        result = started.get();
    }
    spdlog::set_default_logger(previous);
    auto const lines = sink->last_formatted();
    auto const wrote = [&lines](std::string_view text) {
        return std::ranges::any_of(lines, [text](std::string const& line) {
            return line.find(text) != std::string::npos;
        });
    };
    CHECK(result == error::operation_failed);
    CHECK(wrote("sv2.enabled needs a build with the option sv2"));
    CHECK_FALSE(wrote("Failed to open database"));
    std::filesystem::remove_all(dir, ec);
}
#endif

TEST_CASE("sv2 listener settings reject invalid ports", "[configuration tests][parser][sv2 template_provider]") {
    for (auto const* port : {"-1", "65536", "invalid"}) {
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        char const* argv[]{"kth", "--sv2.port", port};
        CHECK_FALSE(parser.parse(3, argv, error));
    }
}

TEST_CASE("outbound proxy settings are empty and randomized by default", "[configuration tests][parser][socks5]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK(defaults.configured.network.proxy.empty());
    CHECK(defaults.configured.network.proxy_randomize);
}

TEST_CASE("I2P settings are empty and accept incoming peers by default", "[configuration tests][parser][i2p]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK(defaults.configured.network.i2psam.empty());
    CHECK(defaults.configured.network.i2p_accept_incoming);
    CHECK(defaults.configured.network.i2p_private_key_file.empty());
}

#if ! defined(KTH_WITH_BLOOM_FILTERS)
// A build without the feature serves no bloom filters, so it does not start as
// if it did.
TEST_CASE("bloom filters are off, and refused, in a build without the feature", "[configuration tests][parser][bloom filters]") {
    node::parser defaults(domain::config::network::regtest);
    CHECK_FALSE(defaults.configured.node.bloom_filters);

    SECTION("on the command line") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.bloom_filters", "true"};
        std::ostringstream error;
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(error.str().find("node.bloom_filters needs a build with the option bloom_filters") != std::string::npos);
    }
    SECTION("in the configuration file") {
        config_file file("bloom_refused", "node.bloom_filters=true");
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        CHECK_FALSE(parser.parse_from_file(file.path(), error));
        CHECK(error.str().find("node.bloom_filters needs a build with the option bloom_filters") != std::string::npos);
    }
    SECTION("turned off, it is what the build does anyway") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--node.bloom_filters", "false"};
        std::ostringstream error;
        CHECK(parser.parse(5, argv, error));
    }
}

TEST_CASE("a node built without bloom filters does not start as one that serves them", "[configuration tests][node][bloom filters]") {
    auto const bloom = uint64_t(domain::message::version::service::node_bloom);
    auto const dir = std::filesystem::temp_directory_path() / ("kth_bloom_node_refused_" + std::to_string(process_id()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    auto const config = [&dir](bool filters) {
        node::configuration cfg{domain::config::network::regtest};
        cfg.database.directory = dir;
        cfg.database.db_max_size = 16ULL << 20;
        cfg.network.threads = 0;
        cfg.network.peers_file = dir / "peers.dat";
        cfg.network.hosts_file = dir / "hosts.dat";
        cfg.node.bloom_filters = filters;
        return cfg;
    };

    node::full_node node{config(true)};
    CHECK((node.network_settings().services & bloom) == 0);

    ::asio::io_context io;
    auto started = ::asio::co_spawn(io, node.start(), ::asio::use_future);
    io.run();
    CHECK(started.get() == error::operation_failed);
    std::filesystem::remove_all(dir, ec);
}
#endif

#if ! defined(KTH_WITH_SOCKS5)
// A build without the feature cannot use a proxy. Taking the setting and going
// on would connect directly, so the node refuses to start.
TEST_CASE("a proxy is refused by a build without the SOCKS5 feature", "[configuration tests][parser][socks5]") {
    SECTION("on the command line") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--net.proxy", "127.0.0.1:9050"};
        std::ostringstream error;
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(error.str().find("net.proxy needs a build with the option socks5") != std::string::npos);
    }
    SECTION("in the configuration file") {
        config_file file("proxy_refused", "net.proxy=127.0.0.1:9050");
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        CHECK_FALSE(parser.parse_from_file(file.path(), error));
        CHECK(error.str().find("net.proxy needs a build with the option socks5") != std::string::npos);
    }
    SECTION("an empty one is no proxy") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--net.proxy", ""};
        std::ostringstream error;
        CHECK(parser.parse(5, argv, error));
    }
}
#endif

#if ! defined(KTH_WITH_I2P)
// A build without the feature has no SAM client, so it does not start as one
// that was asked for I2P peers.
TEST_CASE("an I2P bridge is refused by a build without the I2P feature", "[configuration tests][parser][i2p]") {
    SECTION("on the command line") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--net.i2psam", "127.0.0.1:7656"};
        std::ostringstream error;
        CHECK_FALSE(parser.parse(5, argv, error));
        CHECK(error.str().find("net.i2psam needs a build with the option i2p") != std::string::npos);
    }
    SECTION("in the configuration file") {
        config_file file("i2p_refused", "net.i2psam=127.0.0.1:7656");
        node::parser parser(domain::config::network::regtest);
        std::ostringstream error;
        CHECK_FALSE(parser.parse_from_file(file.path(), error));
        CHECK(error.str().find("net.i2psam needs a build with the option i2p") != std::string::npos);
    }
    SECTION("an empty one is no bridge") {
        node::parser parser(domain::config::network::regtest);
        char const* argv[]{"kth", "--network", "regtest", "--net.i2psam", ""};
        std::ostringstream error;
        CHECK(parser.parse(5, argv, error));
    }
}

TEST_CASE("a node built without I2P does not start as one that uses it", "[configuration tests][node][i2p]") {
    auto const dir = std::filesystem::temp_directory_path() / ("kth_i2p_node_refused_" + std::to_string(process_id()));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // A configuration built by hand, which no parser checked.
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = dir;
    cfg.database.db_max_size = 16ULL << 20;
    cfg.network.threads = 0;
    cfg.network.peers_file = dir / "peers.dat";
    cfg.network.hosts_file = dir / "hosts.dat";
    cfg.network.i2psam = "127.0.0.1:7656";

    node::full_node node{cfg};
    ::asio::io_context io;
    auto started = ::asio::co_spawn(io, node.start(), ::asio::use_future);
    io.run();
    CHECK(started.get() == error::operation_failed);
    std::filesystem::remove_all(dir, ec);
}
#endif

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

TEST_CASE("validation_batch_blocks defaults to 20000 and accepts 1..20000", "[configuration tests][parser]") {
    CHECK(blockchain::settings(domain::config::network::mainnet).validation_batch_blocks == 20000u);
    {
        std::ostringstream error;
        config_file file("batch_blocks", "chain.validation_batch_blocks=250");
        node::parser parser(domain::config::network::mainnet);
        REQUIRE(parser.parse_from_file(file.path(), error));
        CHECK(parser.configured.chain.validation_batch_blocks == 250u);
    }
    for (char const* refused : {"0", "20001"}) {
        std::ostringstream error;
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--chain.validation_batch_blocks", refused};
        CHECK_FALSE(parser.parse(5, argv, error));
    }
}

TEST_CASE("validation_batch_bytes defaults to 512 MiB and refuses zero", "[configuration tests][parser]") {
    CHECK(blockchain::settings(domain::config::network::mainnet).validation_batch_bytes == (uint64_t{512} << 20));
    {
        std::ostringstream error;
        config_file file("batch_bytes", "chain.validation_batch_bytes=1048576");
        node::parser parser(domain::config::network::mainnet);
        REQUIRE(parser.parse_from_file(file.path(), error));
        CHECK(parser.configured.chain.validation_batch_bytes == 1048576u);
    }
    {
        std::ostringstream error;
        node::parser parser(domain::config::network::mainnet);
        char const* argv[]{"kth", "--network", "mainnet", "--chain.validation_batch_bytes", "0"};
        CHECK_FALSE(parser.parse(5, argv, error));
    }
}
