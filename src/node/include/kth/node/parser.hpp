// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PARSER_HPP
#define KTH_NODE_PARSER_HPP

#include <ostream>

#include <kth/domain.hpp>
#include <kth/domain/config/parser.hpp>

#include <kth/node/define.hpp>
#include <kth/node/configuration.hpp>

namespace kth::node {

/// Parse configurable values from environment variables, settings file, and
/// command line positional and non-positional options.
class KND_API parser : public domain::config::parser<parser> {
public:
    parser(domain::config::network context);
    parser(configuration const& defaults);

    /// Parse all configuration into member settings.
    // virtual
    bool parse(int argc, char const* argv[], std::ostream& error);

    // virtual
    bool parse_from_file(kth::path const& config_path, std::ostream& error);

    void set_default_configuration();

    /// Load command line options (named).
    // virtual
    domain::options_metadata load_options();

    /// Load command line arguments (positional).
    // virtual
    domain::arguments_metadata load_arguments();

    /// Load configuration file settings.
    // virtual
    domain::options_metadata load_settings();

    /// Load environment variable settings.
    // virtual
    domain::options_metadata load_environment();

    /// The populated configuration settings values.
    configuration configured;
};

} // namespace kth::node

#endif
