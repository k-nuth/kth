// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <print>

#include <kth/blockchain.hpp>
#include <kth/database.hpp>

using namespace kth;
using namespace kth::blockchain;
using namespace kd::chain;
using namespace kth::database;
using namespace std::filesystem;
using namespace boost::system;

// Create a new mainnet blockchain database.
int main(int argc, char** argv) {
    std::string prefix("mainnet");

    if (argc > 1) {
        prefix = argv[1];
    }

    if (argc > 2 && std::string("--clean") == argv[2]) {
        std::filesystem::remove_all(prefix);
    }

    error_code code;
    if ( ! create_directories(prefix, code)) {
        if (code.value() == 0) {
            std::println(stderr, "Failed because the directory {} already exists.", prefix);
        } else {
            std::println(stderr, "Failed to create directory {} with error, '{}'.", prefix, code.message());
        }
        return -1;
    }

    database::settings settings(domain::config::network::mainnet);

    if ( ! data_base(settings).create(block::genesis_mainnet())) {
        std::println(stderr, "Failed to initialize blockchain files.");
        return -1;
    }

    return 0;
}
