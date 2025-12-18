// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdio>
#include <format>
#include <iostream>
#include <string_view>

#include <kth/node.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/executor/executor_info.hpp>
#include <kth/domain/version.hpp>

KTH_USE_MAIN

static auto const application_name = "kth";

std::string version() {
    return std::format("Knuth v{}", kth::version);
}

void do_help(kth::node::parser& metadata, std::ostream& output) {
    auto const options = metadata.load_options();
    kth::infrastructure::config::printer help(options, application_name, KTH_INFORMATION_MESSAGE);
    help.initialize();
    help.commandline(output);
}

void do_settings(kth::node::parser& metadata, std::ostream& output) {
    auto const settings = metadata.load_settings();
    kth::infrastructure::config::printer print(settings, application_name, KTH_SETTINGS_MESSAGE);
    print.initialize();
    print.settings(output);
}

bool run(kth::node::executor& host) {
    return host.init_run_and_wait_for_signal(version(), kth::node::start_modules::all, [&host](std::error_code const& ec) {
        if (ec != kth::error::success) {
            host.signal_stop();
        }
    });
}

bool menu(kth::node::parser& metadata, kth::node::executor& host, std::ostream& output) {
    auto const& config = metadata.configured;

    if (config.help) {
        do_help(metadata, output);
        return true;
    }

    if (config.settings) {
        do_settings(metadata, output);
        return true;
    }

    if (config.version) {
        host.print_version(version());
        return true;
    }

#if ! defined(KTH_DB_READONLY)
    if (config.init_and_run) {
        if ( ! host.verify_directory()) {
            auto res = host.do_initchain(version());
            if ( ! res) {
                return res;
            }
        }
    } else if (config.initchain) {
        return host.do_initchain(version());
    }
#endif // ! defined(KTH_DB_READONLY)

    // There are no command line arguments, just run the node.
    return run(host);
}

int kth::main(int argc, char* argv[]) {
    using namespace kd;
    using namespace kth::node;
    using namespace kth;

    set_utf8_stdio();
    auto const& args = const_cast<const char**>(argv);

    node::parser metadata(kth::domain::config::network::mainnet);
    if ( ! metadata.parse(argc, args, cerr)) {
        return console_result::failure;
    }

    // executor host(metadata.configured, cout, cerr);
    executor host(metadata.configured, true);
    return menu(metadata, host, cout) ? console_result::okay : console_result::failure;
}
