// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database.hpp>

#include <filesystem>
#include <test_helpers.hpp>

using namespace boost::system;
using namespace std::filesystem;
using namespace kth::domain::chain;
using namespace kth::database;

#define DIRECTORY "transaction_database"

class transaction_database_directory_setup_fixture {
  public:
    transaction_database_directory_setup_fixture() {
        std::error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));
    }

    ////~transaction_database_directory_setup_fixture() {
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////}
};

BOOST_FIXTURE_TEST_SUITE(database_tests, transaction_database_directory_setup_fixture)

// End Test Suite
