// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database.hpp>

#include <filesystem>
#include <test_helpers.hpp>

using namespace boost::system;
using namespace std::filesystem;
using namespace kth::database;

#define DIRECTORY "structure"

class structure_directory_setup_fixture {
  public:
    structure_directory_setup_fixture() {
        std::error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));
    }

    ////~structure_directory_setup_fixture() {
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////}
};

BOOST_FIXTURE_TEST_SUITE(structure_tests, structure_directory_setup_fixture)

TEST_CASE("hash table header  test", "[None]") {
    store::create(DIRECTORY "/hash_table_header");
    memory_map file(DIRECTORY "/hash_table_header");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
    file.resize(4 + 4 * 10);

    hash_table_header<uint32_t, uint32_t> header(file, 10);
    REQUIRE(header.create());
    REQUIRE(header.start());

    header.write(9, 110);
    REQUIRE(header.read(9) == 110);
}

TEST_CASE("slab manager  test", "[None]") {
    store::create(DIRECTORY "/slab_manager");
    memory_map file(DIRECTORY "/slab_manager");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
    file.resize(200);

    slab_manager data(file, 0);
    REQUIRE(data.create());
    REQUIRE(data.start());

    file_offset position = data.new_slab(100);
    REQUIRE(position == 8);
    // slab_byte_pointer slab = data.get(position);

    file_offset position2 = data.new_slab(100);
    REQUIRE(position2 == 108);
    // slab = data.get(position2);

    REQUIRE(file.size() >= 208);
}

TEST_CASE("record manager  test", "[None]") {
    store::create(DIRECTORY "/record_manager");
    memory_map file(DIRECTORY "/record_manager");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
    file.resize(4);

    record_manager recs(file, 0, 10);
    REQUIRE(recs.create());
    REQUIRE(recs.start());

    array_index idx = recs.new_records(1);
    REQUIRE(idx == 0);
    idx = recs.new_records(1);
    REQUIRE(idx == 1);
    REQUIRE(file.size() >= 2 * 10 + 4);
    recs.sync();
}

TEST_CASE("record list  test", "[None]") {
    // TODO
}

// End Test Suite
