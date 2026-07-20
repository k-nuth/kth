// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/configuration.hpp>

#include <cstddef>

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif

namespace kth::node {

// Construct with defaults derived from given context.
configuration::configuration(domain::config::network net)
    : help(false)
#if ! defined(KTH_DB_READONLY)
    , initchain(false)
    , init_and_run(false)
#endif
    , settings(false)
    , version(false)
    , net(net)
    , node(net)
    , chain(net)
    , database(net)
#if ! defined(__EMSCRIPTEN__)
    , network(net)
#endif
{}

// Copy constructor.
configuration::configuration(configuration const& other)
    : help(other.help)
#if ! defined(KTH_DB_READONLY)
    , initchain(other.initchain)
    , init_and_run(other.init_and_run)
#endif
    , settings(other.settings)
    , version(other.version)
    , net(other.net)
    , file(other.file)
    , node(other.node)
    , chain(other.chain)
    , database(other.database)
#if ! defined(__EMSCRIPTEN__)
    , network(other.network)
#endif
{}

} // namespace kth::node
