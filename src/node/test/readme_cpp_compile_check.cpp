// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Verbatim copy of the C++ snippet in `README.md`'s "Using the C++
// library" section. Compiled — but not executed — under
// `ENABLE_TEST=ON`. Its purpose is purely to fail the build if the
// public surface the README example relies on (configuration ctor,
// full_node ctor, start_chain / chain() / fetch_last_height / close
// shapes) drifts.
//
// The runtime smoke for the same flow lives in
// `readme_cpp_runtime.cpp`, which exercises the APIs end-to-end on
// a regtest temp DB.
//
// Keep this file byte-identical to the README's `int main()` body
// minus the includes header.

#include <latch>
#include <print>
#include <system_error>

#include <kth/node.hpp>

int main() {
    // Mainnet defaults — equivalent to what the node-exe ships with.
    kth::node::configuration cfg{kth::domain::config::network::mainnet};

    // Override individual settings if you need to, e.g.:
    //   cfg.database.directory = "/var/lib/kth";
    //   cfg.network.threads    = 8;

    kth::node::full_node node{cfg};

    std::latch done{1};
    node.start_chain([&](std::error_code const& ec) {
        if (ec) { done.count_down(); return; }
        node.chain().fetch_last_height([&](std::error_code const& fec, std::size_t height) {
            if ( ! fec) std::println("Current height: {}", height);
            done.count_down();
        });
    });
    done.wait();
    node.close();
}
