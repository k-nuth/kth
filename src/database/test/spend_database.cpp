// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>

#include <test_helpers.hpp>
#include <kth/database.hpp>

using namespace boost::system;
using namespace std::filesystem;
using namespace kth::domain::chain;
using namespace kth::database;

#define DIRECTORY "spend_database"

class spend_database_directory_setup_fixture {
public:
    spend_database_directory_setup_fixture() {
        std::error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));
    }

    ////~spend_database_directory_setup_fixture() {
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////}
};

BOOST_FIXTURE_TEST_SUITE(database_tests, spend_database_directory_setup_fixture)

#ifdef KTH_DB_SPEND
TEST_CASE("spend database  test", "[None]") {
    domain::chain::output_point key1{ "4129e76f363f9742bc98dd3d40c99c9066e4d53b8e10e5097bd6f7b5059d7c53"_hash, 110 };
    domain::chain::output_point key2{ "eefa5d23968584be9d8d064bcf99c24666e4d53b8e10e5097bd6f7b5059d7c53"_hash, 4 };
    domain::chain::output_point key3{ "4129e76f363f9742bc98dd3d40c99c90eefa5d23968584be9d8d064bcf99c246"_hash, 8 };
    domain::chain::output_point key4{ "80d9e7012b5b171bf78e75b52d2d149580d9e7012b5b171bf78e75b52d2d1495"_hash, 9 };

    domain::chain::input_point value1{ "4742b3eac32d35961f9da9d42d495ff1d90aba96944cac3e715047256f7016d1"_hash, 1 };
    domain::chain::input_point value2{ "d90aba96944cac3e715047256f7016d1d90aba96944cac3e715047256f7016d1"_hash, 2 };
    domain::chain::input_point value3{ "3cc768bbaef30587c72c6eba8dbf6aeec4ef24172ae6fe357f2e24c2b0fa44d5"_hash, 3 };
    domain::chain::input_point value4{ "4742b3eac32d35961f9da9d42d495ff13cc768bbaef30587c72c6eba8dbf6aee"_hash, 4 };

    store::create(DIRECTORY "/spend");
    spend_database db(DIRECTORY "/spend", 1000, 50);
    REQUIRE(db.create());

    db.store(key1, value1);
    db.store(key2, value2);
    db.store(key3, value3);

    // Test fetch.
    auto const spend1 = db.get(key1);
    REQUIRE(spend1.is_valid());
    REQUIRE(spend1.hash() == value1.hash());
    REQUIRE(spend1.index() == value1.index());

    auto const spend2 = db.get(key2);
    REQUIRE(spend2.is_valid());
    REQUIRE(spend2.hash() == value2.hash());
    REQUIRE(spend2.index() == value2.index());

    auto const spend3 = db.get(key3);
    REQUIRE(spend3.is_valid());
    REQUIRE(spend3.hash() == value3.hash());
    REQUIRE(spend3.index() == value3.index());

    // Record shouldnt exist yet.
    REQUIRE( ! db.get(key4).is_valid());

    // Delete record.
    db.unlink(key3);
    REQUIRE( ! db.get(key3).is_valid());

    // Add another record.
    db.store(key4, value4);

    // Fetch it.
    auto const spend4 = db.get(key4);
    REQUIRE(spend4.is_valid());
    REQUIRE(spend4.hash() == value4.hash());
    REQUIRE(spend4.index() == value4.index());
    db.synchronize();
}
#endif // KTH_DB_SPEND

// End Test Suite
