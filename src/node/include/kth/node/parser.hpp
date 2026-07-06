// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PARSER_HPP
#define KTH_NODE_PARSER_HPP

#include <ostream>

#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/path.hpp>

#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

/// Parse configurable values from environment variables, settings file, and
/// command line positional and non-positional options.
///
/// Backed by CLI11 (no more Boost.Program_options).
struct KND_API parser {
    explicit parser(domain::config::network context);
    explicit parser(configuration const& defaults);

    /// Parse command line + KTH_* env vars + config file (if provided).
    bool parse(int argc, char const* argv[], std::ostream& error);

    /// Load settings directly from a config file path (no argv/env).
    bool parse_from_file(kth::path const& config_path, std::ostream& error);

    void set_default_configuration();

    /// Full CLI help text (options + arguments), matches `--help`.
    [[nodiscard]] std::string help_text(std::string const& application,
                                        std::string const& description = "") const;

    /// The current settings expressed as an INI-style config-file snippet.
    [[nodiscard]] std::string settings_text() const;

    /// The populated configuration settings values.
    configuration configured;
};

} // namespace kth::node

#endif
