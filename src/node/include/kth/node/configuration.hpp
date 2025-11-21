// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_CONFIGURATION_HPP
#define KTH_NODE_CONFIGURATION_HPP

#include <filesystem>

#include <kth/blockchain.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif

#include <kth/node/define.hpp>
#include <kth/node/settings.hpp>

// Not localizable.
#define KTH_HELP_VARIABLE "help"
#define KTH_SETTINGS_VARIABLE "settings"
#define KTH_VERSION_VARIABLE "version"

// This must be lower case but the env var part can be any case.
#define KTH_CONFIG_VARIABLE "config"
#define KTH_NETWORK_VARIABLE "network"

// This must match the case of the env var.
#define KTH_ENVIRONMENT_VARIABLE_PREFIX "KTH_"


namespace kth::node {

/// Full node configuration, thread safe.
struct KND_API configuration {
    configuration(domain::config::network net);
    configuration(configuration const& other);

    /// Options.
    bool help;

#if ! defined(KTH_DB_READONLY)
    bool initchain;
    bool init_and_run;
#endif

    bool settings;
    bool version;
    domain::config::network net = domain::config::network::mainnet;

    /// Options and environment vars.
    kth::path file;

    /// Settings.
    node::settings node;
    blockchain::settings chain;
    database::settings database;
#if ! defined(__EMSCRIPTEN__)
    network::settings network;
#endif
};

} // namespace kth::node

#endif
