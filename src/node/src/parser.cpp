// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/parser.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/blockchain.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/full_node.hpp>
#include <kth/node/settings.hpp>

#include <kth/infrastructure/config/directory.hpp>

namespace kth::domain::config {

void validate(boost::any& v, std::vector<std::string> const& values, network* target_type, int) {
    using namespace boost::program_options;

    validators::check_first_occurrence(v);
    auto const& s = validators::get_single_string(values);

    if (s == "mainnet") {
        v = boost::any(network::mainnet);
    } else if (s == "testnet") {
        v = boost::any(network::testnet);
    } else if (s == "regtest") {
        v = boost::any(network::regtest);
#if defined(KTH_CURRENCY_BCH)
    } else if (s == "testnet4") {
        v = boost::any(network::testnet4);
    } else if (s == "scalenet") {
        v = boost::any(network::scalenet);
    } else if (s == "chipnet") {
        v = boost::any(network::chipnet);
#endif
    } else {
        throw validation_error(validation_error::invalid_option_value);
    }
}

} // namespace kth::domain::config


// TODO: localize descriptions.
namespace kth::node {

using namespace std::filesystem;
using namespace boost::program_options;
using namespace kth::domain;
using namespace kth::domain::config;

// Initialize configuration by copying the given instance.
parser::parser(configuration const& defaults)
    : configured(defaults)
{}

void parser::set_default_configuration() {
    // kth_node use history
    using serve = domain::message::version::service;

#if ! defined(__EMSCRIPTEN__)
    // A node allows 8 inbound connections by default.
    configured.network.inbound_connections = 8;
    // Logs will slow things if not rotated.
    configured.network.rotation_size = 10000000;

    // With block-first sync the count should be low until complete.
    configured.network.outbound_connections = 2;

    // A node allows 1000 host names by default.
    configured.network.host_pool_capacity = 1000;

    // Expose full node (1) services by default.
    configured.network.services = serve::node_network;
#endif
}

// Initialize configuration using defaults of the given context.
parser::parser(domain::config::network context)
    : configured(context)
{
    set_default_configuration();
}

options_metadata parser::load_options() {
    options_metadata description("options");
    description.add_options() (
        KTH_NETWORK_VARIABLE ",n",
        value<domain::config::network>(&configured.net),
#if defined(KTH_CURRENCY_BCH)
        "Specify the network (mainnet, testnet, regtest, testnet4, scalenet, chipnet)."
#else
        "Specify the network (mainnet, testnet, regtest)."
#endif
    )(
        KTH_CONFIG_VARIABLE ",c",
        value<path>(&configured.file),
        "Specify path to a configuration settings file."
    )(
        KTH_HELP_VARIABLE ",h",
        value<bool>(&configured.help)->
            default_value(false)->zero_tokens(),
        "Display command line options."
    )
#if ! defined(KTH_DB_READONLY)
    (
        "initchain,i",
        value<bool>(&configured.initchain)->
            default_value(false)->zero_tokens(),
        "Initialize blockchain in the configured directory."
    )
    (
        "init_run,r",
        value<bool>(&configured.init_and_run)->
            default_value(false)->zero_tokens(),
        "Initialize blockchain in the configured directory, then start the node."
    )
#endif // ! defined(KTH_DB_READONLY)
    (
        KTH_SETTINGS_VARIABLE ",s",
        value<bool>(&configured.settings)->
            default_value(false)->zero_tokens(),
        "Display all configuration settings."
    )(
        KTH_VERSION_VARIABLE ",v",
        value<bool>(&configured.version)->
            default_value(false)->zero_tokens(),
        "Display version information."
    );

    return description;
}

arguments_metadata parser::load_arguments() {
    arguments_metadata description;
    return description.add(KTH_CONFIG_VARIABLE, 1);
}

options_metadata parser::load_environment() {
    options_metadata description("environment");
    description.add_options()(
        // For some reason po requires this to be a lower case name.
        // The case must match the other declarations for it to compose.
        // This composes with the cmdline options and inits to system path.
        KTH_CONFIG_VARIABLE,
        value<path>(&configured.file)->composing()
            ->default_value(infrastructure::config::config_default_path()),
        "The path to the configuration settings file."
    );

    return description;
}

options_metadata parser::load_settings() {
    options_metadata description("settings");
    description.add_options()

#if ! defined(__EMSCRIPTEN__)

    /* [log] */
    (
        "log.debug_file",
        value<path>(&configured.network.debug_file),
        "The debug log file path, defaults to 'debug.log'."
    )(
        "log.error_file",
        value<path>(&configured.network.error_file),
        "The error log file path, defaults to 'error.log'."
    )(
        "log.archive_directory",
        value<path>(&configured.network.archive_directory),
        "The log archive directory, defaults to 'archive'."
    )(
        "log.rotation_size",
        value<size_t>(&configured.network.rotation_size),
        "The size at which a log is archived, defaults to 10000000 (0 disables)."
    )(
        "log.minimum_free_space",
        value<size_t>(&configured.network.minimum_free_space),
        "The minimum free space required in the archive directory, defaults to 0."
    )(
        "log.maximum_archive_size",
        value<size_t>(&configured.network.maximum_archive_size),
        "The maximum combined size of archived logs, defaults to 0 (maximum)."
    )(
        "log.maximum_archive_files",
        value<size_t>(&configured.network.maximum_archive_files),
        "The maximum number of logs to archive, defaults to 0 (maximum)."
    )(
        "log.statistics_server",
        value<infrastructure::config::authority>(&configured.network.statistics_server),
        "The address of the statistics collection server, defaults to none."
    )(
        "log.verbose",
        value<bool>(&configured.network.verbose),
        "Enable verbose logging, defaults to false."
    )

    /* [network] */
    (
        "network.threads",
        value<uint32_t>(&configured.network.threads),
        "The minimum number of threads in the network threadpool, defaults to 0 (physical cores)."
    )(
        "network.protocol_maximum",
        value<uint32_t>(&configured.network.protocol_maximum),
        "The maximum network protocol version, defaults to 70013."
    )(
        "network.protocol_minimum",
        value<uint32_t>(&configured.network.protocol_minimum),
        "The minimum network protocol version, defaults to 31402."
    )(
        "network.services",
        value<uint64_t>(&configured.network.services),
        "The services exposed by network connections, defaults to 1 (full node BCH)"
    )(
        "network.invalid_services",
        value<uint64_t>(&configured.network.invalid_services),
        "The advertised services that cause a peer to be dropped, defaults to 176 (BTC) and 0 (BCH)."
    )(
        "network.validate_checksum",
        value<bool>(&configured.network.validate_checksum),
        "Validate the checksum of network messages, defaults to false."
    )(
        "network.identifier",
        value<uint32_t>(&configured.network.identifier),
        "The magic number for message headers, defaults to 3652501241."
    )(
        "network.inbound_port",
        value<uint16_t>(&configured.network.inbound_port),
        "The port for incoming connections, defaults to 8333."
    )(
        "network.inbound_connections",
        value<uint32_t>(&configured.network.inbound_connections),
        "The target number of incoming network connections, defaults to 0."
    )(
        "network.outbound_connections",
        value<uint32_t>(&configured.network.outbound_connections),
        "The target number of outgoing network connections, defaults to 2."
    )(
        "network.manual_attempt_limit",
        value<uint32_t>(&configured.network.manual_attempt_limit),
        "The attempt limit for manual connection establishment, defaults to 0 (forever)."
    )(
        "network.connect_batch_size",
        value<uint32_t>(&configured.network.connect_batch_size),
        "The number of concurrent attempts to establish one connection, defaults to 5."
    )(
        "network.connect_timeout_seconds",
        value<uint32_t>(&configured.network.connect_timeout_seconds),
        "The time limit for connection establishment, defaults to 5."
    )(
        "network.channel_handshake_seconds",
        value<uint32_t>(&configured.network.channel_handshake_seconds),
        "The time limit to complete the connection handshake, defaults to 30."
    )(
        "network.channel_heartbeat_minutes",
        value<uint32_t>(&configured.network.channel_heartbeat_minutes),
        "The time between ping messages, defaults to 5."
    )(
        "network.channel_inactivity_minutes",
        value<uint32_t>(&configured.network.channel_inactivity_minutes),
        "The inactivity time limit for any connection, defaults to 30."
    )(
        "network.channel_expiration_minutes",
        value<uint32_t>(&configured.network.channel_expiration_minutes),
        "The age limit for any connection, defaults to 60."
    )(
        "network.channel_germination_seconds",
        value<uint32_t>(&configured.network.channel_germination_seconds),
        "The time limit for obtaining seed addresses, defaults to 30."
    )(
        "network.host_pool_capacity",
        value<uint32_t>(&configured.network.host_pool_capacity),
        "The maximum number of peer hosts in the pool, defaults to 1000."
    )(
        "network.hosts_file",
        value<path>(&configured.network.hosts_file),
        "The peer hosts cache file path, defaults to 'hosts.cache'."
    )(
        "network.self",
        value<infrastructure::config::authority>(&configured.network.self),
        "The advertised public address of this node, defaults to none."
    )(
        "network.blacklist",
        value<infrastructure::config::authority::list>(&configured.network.blacklist),
        "IP address to disallow as a peer, multiple entries allowed."
    )(
        "network.peer",
        value<infrastructure::config::endpoint::list>(&configured.network.peers),
        "A persistent peer node, multiple entries allowed."
    )(
        "network.seed",
        value<infrastructure::config::endpoint::list>(&configured.network.seeds),
        "A seed node for initializing the host pool, multiple entries allowed."
    )(
        "network.use_ipv6",
        value<bool>(&configured.network.use_ipv6),
        "Node use ipv6."
    )(
        "network.user_agent_blacklist",
        value<std::vector<std::string>>(&configured.network.user_agent_blacklist),
        "Blacklist user-agent starting with..."
    )
#endif

    /* [database] */
    (
        "database.db_mode",
        value<kth::database::db_mode_type>(&configured.database.db_mode),
        "The DB storage/indexation mode (pruned, blocks, full), defaults to blocks."
    )(
        "database.directory",
        value<path>(&configured.database.directory),
        "The blockchain database directory, defaults to 'blockchain'."
    )(
        "database.reorg_pool_limit",
        value<uint32_t>(&configured.database.reorg_pool_limit),
        "Approximate number of blocks to store in the reorganization pool, defaults to 100."        //TODO(fernando): look for a good default
    )(
        "database.db_max_size",
        value<uint64_t>(&configured.database.db_max_size),
        "Maximum size of the database expressed in bytes, defaults to 100 GiB."                    //TODO(fernando): look for a good default
    )(
        "database.safe_mode",
        value<bool>(&configured.database.safe_mode),
        "safe mode is more secure but not the fastest, defaults to true."
    )(
        "database.cache_capacity",
        value<uint32_t>(&configured.database.cache_capacity),
        "The maximum number of entries in the unspent outputs cache, defaults to 10000."
    )
    /* [blockchain] */
    (
        "blockchain.cores",
        value<uint32_t>(&configured.chain.cores),
        "The number of cores dedicated to block validation, defaults to 0 (physical cores)."
    )(
        "blockchain.priority",
        value<bool>(&configured.chain.priority),
        "Use high thread priority for block validation, defaults to true."
    )
    // (
    //     "blockchain.use_libconsensus",
    //     value<bool>(&configured.chain.use_libconsensus),
    //     "Use libconsensus for script validation if integrated, defaults to false."
    // )
    (
        "blockchain.reorganization_limit",
        value<uint32_t>(&configured.chain.reorganization_limit),
        "The maximum reorganization depth, defaults to 256 (0 for unlimited)."
    )(
        "blockchain.checkpoint",
        value<infrastructure::config::checkpoint::list>(&configured.chain.checkpoints),
        "A hash:height checkpoint, multiple entries allowed."
    )
    (
        "blockchain.fix_checkpoints",
        value<bool>(&configured.chain.fix_checkpoints),
        "Uses the hardcoded checkpoints and the user defined ones, defaults to true."
    )



    /* [fork] */
    (
        "fork.easy_blocks",
        value<bool>(&configured.chain.easy_blocks),
        "Allow minimum difficulty blocks, defaults to false."
    )(
        "fork.retarget",
        value<bool>(&configured.chain.retarget),
        "Retarget difficulty, defaults to true."
    )(
        "fork.bip16",
        value<bool>(&configured.chain.bip16),
        "Add pay-to-script-hash processing, defaults to true (soft fork)."
    )(
        "fork.bip30",
        value<bool>(&configured.chain.bip30),
        "Disallow collision of unspent transaction hashes, defaults to true (soft fork)."
    )(
        "fork.bip34",
        value<bool>(&configured.chain.bip34),
        "Require coinbase input includes block height, defaults to true (soft fork)."
    )(
        "fork.bip66",
        value<bool>(&configured.chain.bip66),
        "Require strict signature encoding, defaults to true (soft fork)."
    )(
        "fork.bip65",
        value<bool>(&configured.chain.bip65),
        "Add check-locktime-verify op code, defaults to true (soft fork)."
    )(
        "fork.bip90",
        value<bool>(&configured.chain.bip90),
        "Assume bip34, bip65, and bip66 activation if enabled, defaults to true (hard fork)."
    )(
        "fork.bip68",
        value<bool>(&configured.chain.bip68),
        "Add relative locktime enforcement, defaults to true (soft fork)."
    )(
        "fork.bip112",
        value<bool>(&configured.chain.bip112),
        "Add check-sequence-verify op code, defaults to true (soft fork)."
    )(
        "fork.bip113",
        value<bool>(&configured.chain.bip113),
        "Use median time past for locktime, defaults to true (soft fork)."
    )

    // BCH only things

    // (
    //     "fork.uahf_height",
    //     value<size_t>(&configured.chain.uahf_height),
    //     "Height of the 2017-Aug-01 hard fork (UAHF), defaults to 478559 (Mainnet)."
    // )
    // (
    //     "fork.daa_height",
    //     value<size_t>(&configured.chain.daa_height),
    //     "Height of the 2017-Nov-13 hard fork (DAA), defaults to 504031 (Mainnet)."
    // )
    // (
    //     "fork.pythagoras_activation_time",
    //     value<uint64_t>(&configured.chain.pythagoras_activation_time),
    //     "Unix time used for MTP activation of 2018-May-15 hard fork, defaults to 1526400000."
    // )
    // (
    //     "fork.euclid_activation_time",
    //     value<uint64_t>(&configured.chain.euclid_activation_time),
    //     "Unix time used for MTP activation of 2018-Nov-15 hard fork, defaults to 1542300000."
    // )
    // (
    //     "fork.pisano_activation_time",
    //     value<uint64_t>(&configured.chain.pisano_activation_time),
    //     "Unix time used for MTP activation of 2019-May-15 hard fork, defaults to 1557921600."
    // )
    // (
    //     "fork.mersenne_activation_time",
    //     value<uint64_t>(&configured.chain.mersenne_activation_time),
    //     "Unix time used for MTP activation of 2019-Nov-15 hard fork, defaults to 1573819200."
    // )
    // (
    //     "fork.fermat_activation_time",
    //     value<uint64_t>(&configured.chain.fermat_activation_time),
    //     "Unix time used for MTP activation of 2020-May-15 hard fork, defaults to 1589544000."
    // )
    // (
    //     "fork.euler_activation_time",
    //     value<uint64_t>(&configured.chain.euler_activation_time),
    //     "Unix time used for MTP activation of 2020-Nov-15 hard fork, defaults to 1605441600."
    // )

    // No HF for 2021-May-15

    // (
    //     "fork.gauss_activation_time",
    //     value<uint64_t>(&configured.chain.gauss_activation_time),
    //     "Unix time used for MTP activation of 2022-May-15 hard fork, defaults to 1652616000."
    // )
    // (
    //     "fork.descartes_activation_time",
    //     value<uint64_t>(&configured.chain.descartes_activation_time),
    //     "Unix time used for MTP activation of 2023-May-15 hard fork, defaults to 1684152000."
    // )
    // (
    //     "fork.lobachevski_activation_time",
    //     value<uint64_t>(&configured.chain.lobachevski_activation_time),
    //     "Unix time used for MTP activation of 2024-May-15 hard fork, defaults to 1715774400."
    // )
    // (
    //     "fork.galois_activation_time",
    //     value<uint64_t>(&configured.chain.galois_activation_time),
    //     "Unix time used for MTP activation of 2025-May-15 hard fork, defaults to 1747310400."
    // )
    (
        "fork.leibniz_activation_time",
        value<uint64_t>(&configured.chain.leibniz_activation_time),
        "Unix time used for MTP activation of 2026-May-15 hard fork, defaults to 1778846400."
    )
    (
        "fork.cantor_activation_time",
        value<uint64_t>(&configured.chain.cantor_activation_time),
        "Unix time used for MTP activation of 2027-May-15 hard fork, defaults to xxxxxxxxx."
    )
    // (
    //     "fork.unnamed_activation_time",
    //     value<uint64_t>(&configured.chain.unnamed_activation_time),
    //     "Unix time used for MTP activation of ????-???-?? hard fork, defaults to 9999999999."
    // )
    (
        "fork.asert_half_life",
        value<uint64_t>(&configured.chain.asert_half_life),
        "The half life for the ASERTi3-2d DAA. For every (asert_half_life) seconds behind schedule the blockchain gets, difficulty is cut in half. Doubled if blocks are ahead of schedule. Defaults to: 2 * 24 * 60 * 60 = 172800 (two days)."
    )



    /* [node] */
    ////(
    ////    "node.sync_peers",
    ////    value<uint32_t>(&configured.node.sync_peers),
    ////    "The maximum number of initial block download peers, defaults to 0 (physical cores)."
    ////)
    ////(
    ////    "node.sync_timeout_seconds",
    ////    value<uint32_t>(&configured.node.sync_timeout_seconds),
    ////    "The time limit for block response during initial block download, defaults to 5."
    ////)
    (
        "node.block_latency_seconds",
        value<uint32_t>(&configured.node.block_latency_seconds),
        "The time to wait for a requested block, defaults to 60."
    )(
        /* Internally this is blockchain, but it is conceptually a node setting. */
        "node.notify_limit_hours",
        value<uint32_t>(&configured.chain.notify_limit_hours),
        "Disable relay when top block age exceeds, defaults to 24 (0 disables)."
    )(
        /* Internally this is blockchain, but it is conceptually a node setting. */
        "node.byte_fee_satoshis",
        value<float>(&configured.chain.byte_fee_satoshis),
        "The minimum fee per byte, cumulative for conflicts, defaults to 1."
    )(
        /* Internally this is blockchain, but it is conceptually a node setting. */
        "node.sigop_fee_satoshis",
        value<float>(&configured.chain.sigop_fee_satoshis),
        "The minimum fee per sigop, additional to byte fee, defaults to 100."
    )(
        /* Internally this is blockchain, but it is conceptually a node setting. */
        "node.minimum_output_satoshis",
        value<uint64_t>(&configured.chain.minimum_output_satoshis),
        "The minimum output value, defaults to 500."
    )
#if ! defined(__EMSCRIPTEN__)
    (
        /* Internally this is network, but it is conceptually a node setting. */
        "node.relay_transactions",
        value<bool>(&configured.network.relay_transactions),
        "Request that peers relay transactions, defaults to true."
    )
#endif
    (
        "node.refresh_transactions",
        value<bool>(&configured.node.refresh_transactions),
        "Request transactions on each channel start, defaults to true."
    )(
        "node.compact_blocks_high_bandwidth",
        value<bool>(&configured.node.compact_blocks_high_bandwidth),
        "Compact Blocks High-Bandwidth mode, default to true."
    )(
        "node.ds_proofs",
        value<bool>(&configured.node.ds_proofs_enabled),
        "Double-Spend Proofs, default to false."
    )

#if defined(KTH_WITH_MEMPOOL)
    (
        "node.mempool_max_template_size",
        value<size_t>(&configured.chain.mempool_max_template_size),
        "Max size of the template block. Default is coin dependant. (BCH: 31,980,000 bytes, BTC and LTC: 3,980,000 bytes)"
    )(
        "node.mempool_size_multiplier",
        value<size_t>(&configured.chain.mempool_size_multiplier),
        "Max mempool size is equal to MaxBlockSize multiplied by mempool_size_multiplier. Default is 10."
    )
#endif
    ;

    return description;
}

domain::config::network get_configured_network(boost::program_options::variables_map& variables) {
    auto const& temp_str = variables[KTH_NETWORK_VARIABLE];
    if (temp_str.empty()) return domain::config::network::mainnet;

    auto const net = temp_str.as<domain::config::network>();
    return net;
}

bool parser::parse(int argc, const char* argv[], std::ostream& error) {
    try {
        load_error file = load_error::non_existing_file;
        variables_map variables;
        load_command_variables(variables, argc, argv);
        load_environment_variables(variables, KTH_ENVIRONMENT_VARIABLE_PREFIX);

        bool version_sett_help = true;
        // Don't load the rest if any of these options are specified.
        if ( ! get_option(variables, KTH_VERSION_VARIABLE) &&
             ! get_option(variables, KTH_SETTINGS_VARIABLE) &&
             ! get_option(variables, KTH_HELP_VARIABLE)) {

            auto const net = get_configured_network(variables);
            // auto new_configured = configuration(net);
            configured = configuration(net);
            set_default_configuration();

            version_sett_help = false;
            // Returns true if the settings were loaded from a file.
            file = load_configuration_variables(variables, KTH_CONFIG_VARIABLE);

            if (file == load_error::non_existing_file) {
                spdlog::error("[node] Config file provided does not exists.");
                return false;
            }
        }

        // Update bound variables in metadata.settings.
        notify(variables);

#if ! defined(__EMSCRIPTEN__)
        if ( ! version_sett_help && configured.chain.fix_checkpoints) {
            fix_checkpoints(configured.network.identifier, configured.chain.checkpoints, configured.network.inbound_port == 48333);
        }
#endif

        // Clear the config file path if it wasn't used.
        if (file == load_error::default_config) {
            configured.file.clear();
        }
    } catch (boost::program_options::error const& e) {
        // This is obtained from boost, which circumvents our localization.
        error << format_invalid_parameter(e.what()) << std::endl;
        return false;
    }

    return true;
}

bool parser::parse_from_file(kth::path const& config_path, std::ostream& error) {
    try {
        variables_map variables;

        configured.file = config_path;
        auto file = load_configuration_variables_path(variables, config_path);

        if (file == load_error::non_existing_file) {
            error << "Config file provided does not exists." << std::endl;
            return false;
        }

        // Update bound variables in metadata.settings.
        notify(variables);

#if ! defined(__EMSCRIPTEN__)
        if (configured.chain.fix_checkpoints) {
            fix_checkpoints(configured.network.identifier, configured.chain.checkpoints, configured.network.inbound_port == 48333);
        }
#endif

        // Clear the config file path if it wasn't used.
        if (file == load_error::default_config) {
            configured.file.clear();
        }

    } catch (boost::program_options::error const& e) {
        // This is obtained from boost, which circumvents our localization.
        error << format_invalid_parameter(e.what()) << std::endl;
        return false;
    }

    return true;
}

} // namespace kth::node