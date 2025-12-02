// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <print>

#include <kth/database.hpp>

using namespace kth::domain::chain;
using namespace kth::database;
using namespace std::filesystem;
using namespace boost::system;

// Create a new mainnet database.
int main(int argc, char** argv) {
    std::string prefix("mainnet");

    if (argc > 1) {
        prefix = argv[1];
    }

    if (argc > 2 && std::string("--clean") == argv[2]) {
        remove_all(prefix);
    }

    std::error_code code;
    if ( ! create_directories(prefix, code)) {
        if (code.value() == 0) {
            std::println(stderr, "Failed because the directory {} already exists.", prefix);
        } else {
            std::println(stderr, "Failed to create directory {} with error, '{}'.", prefix, code.message());
        }
        return -1;
    }

    // This creates default configuration database only!
    settings const configuration;

    if ( ! data_base(configuration).create(block::genesis_mainnet())) {
        std::println(stderr, "Failed to initialize database files.");
        return -1;
    }

    return 0;
}
