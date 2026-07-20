// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/parser.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <kth/blockchain.hpp>
#include <kth/database/databases/property_code.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/domain/config/parser.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/config/directory.hpp>
#include <kth/infrastructure/config/endpoint.hpp>
#include <kth/infrastructure/display_mode.hpp>

#include <kth/node/full_node.hpp>
#include <kth/node/settings.hpp>

namespace kth::node {

using namespace kth::domain::config;
using kth::infrastructure::config::authority;
using kth::infrastructure::config::checkpoint;
using kth::infrastructure::config::endpoint;

namespace {

// ---- CLI11 <-> parse_from helpers ----------------------------------------

// Bind `--flag value` to `target`, converting via `T::parse_from`.
template <typename T>
void add_parse_from_option(CLI::App& app,
                           std::string const& name,
                           T& target,
                           std::string const& desc) {
    app.add_option_function<std::string>(name,
        [&target, name](std::string const& v) {
            auto r = T::parse_from(v);
            if ( ! r) {
                throw CLI::ValidationError(name, r.error().message());
            }
            target = std::move(*r);
        }, desc);
}

// Bind `--flag value` (repeatable) to `target` as a list.
template <typename T>
void add_parse_from_list(CLI::App& app,
                         std::string const& name,
                         std::vector<T>& target,
                         std::string const& desc) {
    app.add_option_function<std::vector<std::string>>(name,
        [&target, name](std::vector<std::string> const& values) {
            for (auto const& v : values) {
                auto r = T::parse_from(v);
                if ( ! r) {
                    throw CLI::ValidationError(name, r.error().message());
                }
                target.push_back(std::move(*r));
            }
        }, desc)->take_all();
}

// display_mode text → enum.
[[nodiscard]] std::expected<display_mode, kth::code>
parse_display_mode(std::string_view s) {
    if (s == "tui"    || s == "TUI")    return display_mode::tui;
    if (s == "log"    || s == "LOG")    return display_mode::log;
    if (s == "daemon" || s == "DAEMON") return display_mode::daemon;
    return std::unexpected(kth::error::illegal_value);
}

// db_mode_type text → enum.
[[nodiscard]] std::expected<kth::database::db_mode_type, kth::code>
parse_db_mode_local(std::string_view s) {
    return kth::database::parse_db_mode(s);
}

// Add a `--flag value` option that runs `parser(v)` and stores in `target`.
template <typename T, typename Parser>
void add_enum_option(CLI::App& app,
                     std::string const& name,
                     T& target,
                     Parser parser,
                     std::string const& desc) {
    app.add_option_function<std::string>(name,
        [&target, parser, name](std::string const& v) {
            auto r = parser(v);
            if ( ! r) {
                throw CLI::ValidationError(name, r.error().message());
            }
            target = *r;
        }, desc);
}

// ---- option/settings registration ---------------------------------------

// Register command-line-only options (--help, --version, --network, ...).
void add_command_options(CLI::App& app, configuration& cfg) {
    add_enum_option(app, "--" KTH_NETWORK_VARIABLE ",-n",
        cfg.net, parse_network,
#if defined(KTH_CURRENCY_BCH)
        "Specify the network (mainnet, testnet, regtest, testnet4, scalenet, chipnet).");
#else
        "Specify the network (mainnet, testnet, regtest).");
#endif

    app.add_option("--" KTH_CONFIG_VARIABLE ",-c", cfg.file,
        "Specify path to a configuration settings file.");

    app.add_flag("--" KTH_HELP_VARIABLE ",-h", cfg.help,
        "Display command line options.");

#if ! defined(KTH_DB_READONLY)
    app.add_flag("--initchain,-i", cfg.initchain,
        "Initialize blockchain in the configured directory.");

    app.add_flag("--init_run,-r", cfg.init_and_run,
        "Initialize blockchain in the configured directory, then start the node.");
#endif

    app.add_flag("--" KTH_SETTINGS_VARIABLE ",-s", cfg.settings,
        "Display all configuration settings.");

    app.add_flag("--" KTH_VERSION_VARIABLE ",-v", cfg.version,
        "Display version information.");

    add_enum_option(app, "--display,-d",
        cfg.node.display, parse_display_mode,
        "Display mode (tui, log, daemon). Default: log.");
}

// Register config-file settings on top of `app`. CLI11 exposes the same
// options via `--section.key` on the command line and via `[section]/key`
// in the INI-style config file.
void add_settings(CLI::App& app, configuration& cfg) {
#if ! defined(__EMSCRIPTEN__)
    // [log]
    app.add_option("--log.debug_file",       cfg.network.debug_file);
    app.add_option("--log.error_file",       cfg.network.error_file);
    app.add_option("--log.archive_directory", cfg.network.archive_directory);
    app.add_option("--log.rotation_size",    cfg.network.rotation_size);
    app.add_option("--log.minimum_free_space", cfg.network.minimum_free_space);
    app.add_option("--log.maximum_archive_size", cfg.network.maximum_archive_size);
    app.add_option("--log.maximum_archive_files", cfg.network.maximum_archive_files);
    add_parse_from_option(app, "--log.statistics_server",
        cfg.network.statistics_server,
        "The address of the statistics collection server.");
    app.add_option("--log.verbose",          cfg.network.verbose);

    // [network]
    app.add_option("--network.threads",              cfg.network.threads);
    app.add_option("--network.protocol_maximum",     cfg.network.protocol_maximum);
    app.add_option("--network.protocol_minimum",     cfg.network.protocol_minimum);
    app.add_option("--network.services",             cfg.network.services);
    app.add_option("--network.invalid_services",     cfg.network.invalid_services);
    app.add_option("--network.validate_checksum",    cfg.network.validate_checksum);
    app.add_option("--network.identifier",           cfg.network.identifier);
    app.add_option("--network.inbound_port",         cfg.network.inbound_port);
    app.add_option("--network.inbound_connections",  cfg.network.inbound_connections);
    app.add_option("--network.outbound_connections", cfg.network.outbound_connections);
    app.add_option("--network.manual_attempt_limit", cfg.network.manual_attempt_limit);
    app.add_option("--network.connect_batch_size",   cfg.network.connect_batch_size);
    app.add_option("--network.connect_timeout_seconds", cfg.network.connect_timeout_seconds);
    app.add_option("--network.channel_handshake_seconds", cfg.network.channel_handshake_seconds);
    app.add_option("--network.channel_heartbeat_minutes", cfg.network.channel_heartbeat_minutes);
    app.add_option("--network.channel_inactivity_minutes", cfg.network.channel_inactivity_minutes);
    app.add_option("--network.channel_expiration_minutes", cfg.network.channel_expiration_minutes);
    app.add_option("--network.channel_germination_seconds", cfg.network.channel_germination_seconds);
    app.add_option("--network.host_pool_capacity",   cfg.network.host_pool_capacity);
    app.add_option("--network.hosts_file",           cfg.network.peers_file);
    add_parse_from_option(app, "--network.self", cfg.network.self,
        "The advertised public address of this node.");
    add_parse_from_list(app, "--network.blacklist", cfg.network.blacklist,
        "IP addresses to disallow as peers.");
    add_parse_from_list(app, "--network.peer", cfg.network.peers,
        "Persistent peer nodes.");
    add_parse_from_list(app, "--network.seed", cfg.network.seeds,
        "Seed nodes for initializing the host pool.");
    app.add_option("--network.use_ipv6",             cfg.network.use_ipv6);
    app.add_option("--network.user_agent_blacklist", cfg.network.user_agent_blacklist)
        ->take_all();
#endif

    // [database]
    add_enum_option(app, "--database.db_mode",
        cfg.database.db_mode, parse_db_mode_local,
        "The DB storage/indexation mode (pruned, blocks, full).");
    app.add_option("--database.directory",        cfg.database.directory);
    app.add_option("--database.reorg_pool_limit", cfg.database.reorg_pool_limit);
    app.add_option("--database.db_max_size",      cfg.database.db_max_size);
    app.add_option("--database.safe_mode",        cfg.database.safe_mode);
    app.add_option("--database.cache_capacity",   cfg.database.cache_capacity);

    // [blockchain]
    app.add_option("--blockchain.cores",                cfg.chain.cores);
    app.add_option("--blockchain.priority",             cfg.chain.priority);
    app.add_option("--blockchain.reorganization_limit", cfg.chain.reorganization_limit);
    add_parse_from_list(app, "--blockchain.checkpoint", cfg.chain.checkpoints,
        "A hash:height checkpoint.");
    app.add_option("--blockchain.fix_checkpoints", cfg.chain.fix_checkpoints);

    // [fork]
    app.add_option("--fork.easy_blocks", cfg.chain.easy_blocks);
    app.add_option("--fork.retarget",    cfg.chain.retarget);
    app.add_option("--fork.bip16",       cfg.chain.bip16);
    app.add_option("--fork.bip30",       cfg.chain.bip30);
    app.add_option("--fork.bip34",       cfg.chain.bip34);
    app.add_option("--fork.bip66",       cfg.chain.bip66);
    app.add_option("--fork.bip65",       cfg.chain.bip65);
    app.add_option("--fork.bip90",       cfg.chain.bip90);
    app.add_option("--fork.bip68",       cfg.chain.bip68);
    app.add_option("--fork.bip112",      cfg.chain.bip112);
    app.add_option("--fork.bip113",      cfg.chain.bip113);

    app.add_option("--fork.leibniz_activation_time", cfg.chain.leibniz_activation_time);
    app.add_option("--fork.cantor_activation_time",  cfg.chain.cantor_activation_time);
    app.add_option("--fork.asert_half_life",         cfg.chain.asert_half_life);

    // [node]
    app.add_option("--node.block_latency_seconds", cfg.node.block_latency_seconds);
    app.add_option("--node.notify_limit_hours",    cfg.chain.notify_limit_hours);
    app.add_option("--node.byte_fee_satoshis",     cfg.chain.byte_fee_satoshis);
    app.add_option("--node.sigop_fee_satoshis",    cfg.chain.sigop_fee_satoshis);
    app.add_option("--node.minimum_output_satoshis", cfg.chain.minimum_output_satoshis);
#if ! defined(__EMSCRIPTEN__)
    app.add_option("--node.relay_transactions", cfg.network.relay_transactions);
#endif
    app.add_option("--node.refresh_transactions",           cfg.node.refresh_transactions);
    app.add_option("--node.compact_blocks_high_bandwidth",  cfg.node.compact_blocks_high_bandwidth);
    app.add_option("--node.ds_proofs",                      cfg.node.ds_proofs_enabled);
    add_enum_option(app, "--node.display",
        cfg.node.display, parse_display_mode,
        "Display mode (tui, log, daemon).");

}

// Read KTH_* env vars once and, for the subset of names that map to CLI11
// options, seed CLI11's internal store. We only need `KTH_CONFIG`
// historically — keep the same behavior.
void apply_environment(configuration& cfg) {
    if (auto const* env_config = std::getenv(KTH_ENVIRONMENT_VARIABLE_PREFIX KTH_CONFIG_VARIABLE);
        env_config != nullptr && *env_config != '\0') {
        // Command-line --config wins over env. Only overwrite if unset.
        if (cfg.file.empty()) {
            cfg.file = env_config;
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------

parser::parser(configuration const& defaults)
    : configured(defaults)
{}

parser::parser(domain::config::network context)
    : configured(context)
{
    set_default_configuration();
}

void parser::set_default_configuration() {
    using serve = domain::message::version::service;

#if ! defined(__EMSCRIPTEN__)
    configured.network.inbound_connections = 8;
    configured.network.rotation_size = 10000000;
    // Headers-first sync allows parallel block downloads from multiple peers.
    // 2026-02-02: Increased from 8 to 32 for better sync performance.
    configured.network.outbound_connections = 32;
    configured.network.host_pool_capacity = 1000;
    configured.network.services = serve::node_network;
#endif
}

bool parser::parse(int argc, char const* argv[], std::ostream& error) {
    CLI::App app{"kth node"};
    app.allow_config_extras();

    add_command_options(app, configured);
    add_settings(app, configured);

    try {
        app.parse(argc, argv);
    } catch (CLI::ParseError const& e) {
        error << "Error: " << e.what() << std::endl;
        return e.get_exit_code() == 0;
    }

    apply_environment(configured);

    // Metadata-only flags short-circuit the rest.
    bool const meta_flag_set = configured.version || configured.settings || configured.help;

    if ( ! meta_flag_set) {
        // Rebuild the settings tree with the freshly-parsed network so the
        // subsequent config-file load overrides the network defaults.
        configuration merged{configured.net};
        merged.file = configured.file;
        merged.help = configured.help;
        merged.settings = configured.settings;
        merged.version = configured.version;
#if ! defined(KTH_DB_READONLY)
        merged.initchain = configured.initchain;
        merged.init_and_run = configured.init_and_run;
#endif
        configured = std::move(merged);
        set_default_configuration();

        auto const& config_path = configured.file;
        if ( ! config_path.empty()) {
            std::error_code ec;
            if ( ! std::filesystem::exists(config_path, ec)) {
                spdlog::error("[node] Config file provided does not exist.");
                return false;
            }

            // Rebuild the app with the merged defaults and re-parse (config
            // file only) so bindings pick up the freshly-defaulted settings.
            CLI::App app2{"kth node"};
            app2.allow_config_extras();
            add_command_options(app2, configured);
            add_settings(app2, configured);

            try {
                app2.set_config("--" KTH_CONFIG_VARIABLE, config_path.string());
                app2.parse(0, static_cast<char const**>(nullptr));
            } catch (CLI::ParseError const& e) {
                error << "Error: " << e.what() << std::endl;
                return e.get_exit_code() == 0;
            }
        } else {
            // No config file used — clear the field so downstream code can
            // detect "used defaults".
            configured.file.clear();
        }

#if ! defined(__EMSCRIPTEN__)
        if (configured.chain.fix_checkpoints) {
            domain::config::fix_checkpoints(configured.network.identifier,
                configured.chain.checkpoints,
                configured.network.inbound_port == 48333);
        }
#endif
    }

    return true;
}

std::string parser::help_text(std::string const& application,
                              std::string const& description) const {
    CLI::App app{description, application};
    // Build against a throwaway config: we only need the option set for
    // rendering help, not for actual assignment.
    auto cfg_copy = configured;
    add_command_options(app, cfg_copy);
    add_settings(app, cfg_copy);
    return app.help();
}

std::string parser::settings_text() const {
    CLI::App app{"", "kth"};
    auto cfg_copy = configured;
    add_command_options(app, cfg_copy);
    add_settings(app, cfg_copy);
    return app.config_to_str(true, true);
}

bool parser::parse_from_file(kth::path const& config_path, std::ostream& error) {
    std::error_code ec;
    if ( ! std::filesystem::exists(config_path, ec)) {
        error << "Config file provided does not exist." << std::endl;
        return false;
    }

    configured.file = config_path;

    CLI::App app{"kth node"};
    app.allow_config_extras();
    add_command_options(app, configured);
    add_settings(app, configured);

    try {
        app.set_config("--" KTH_CONFIG_VARIABLE, config_path.string());
        app.parse(0, static_cast<char const**>(nullptr));
    } catch (CLI::ParseError const& e) {
        error << "Error: " << e.what() << std::endl;
        return e.get_exit_code() == 0;
    }

#if ! defined(__EMSCRIPTEN__)
    if (configured.chain.fix_checkpoints) {
        domain::config::fix_checkpoints(configured.network.identifier,
            configured.chain.checkpoints,
            configured.network.inbound_port == 48333);
    }
#endif

    return true;
}

} // namespace kth::node
