// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <iostream>

#define FMT_HEADER_ONLY 1
#include <kth/database.hpp>

#include <fmt/core.h>

// #define BS_INITCHAIN_DIR_NEW "Failed to create directory %1% with error, '%2%'.\n"
// #define BS_INITCHAIN_DIR_EXISTS "Failed because the directory %1% already exists.\n"
#define BS_INITCHAIN_DIR_NEW "Failed to create directory {} with error, '{}'.\n"
#define BS_INITCHAIN_DIR_EXISTS "Failed because the directory {} already exists.\n"
#define BS_INITCHAIN_FAIL "Failed to initialize database files.\n"

using namespace kth::domain::chain;
using namespace kth::database;
using namespace std::filesystem;
using namespace boost::system;
// using boost::format;

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
    if (! create_directories(prefix, code)) {
        if (code.value() == 0) {
            std::cerr << fmt::format(BS_INITCHAIN_DIR_EXISTS, prefix);
        } else {
            std::cerr << fmt::format(BS_INITCHAIN_DIR_NEW, prefix, code.message());
        }
        return -1;
    }

    // This creates default configuration database only!
    const settings configuration;

    if (! data_base(configuration).create(block::genesis_mainnet())) {
        std::cerr << BS_INITCHAIN_FAIL;
        return -1;
    }

    return 0;
}
