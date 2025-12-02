// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <random>

#include <boost/functional/hash_fwd.hpp>
#include <test_helpers.hpp>

#include <kth/database.hpp>

using namespace boost::system;
using namespace std::filesystem;
using namespace kth::database;

constexpr size_t total_txs = 200;
constexpr size_t tx_size = 200;
constexpr size_t buckets = 100;
#define DIRECTORY "hash_table"

typedef byte_array<4> tiny_hash;
typedef byte_array<8> little_hash;

// Extend std namespace with tiny_hash wrapper.
namespace std {

template <>
struct hash<tiny_hash> {
    size_t operator()(tiny_hash const& value) const {
        return boost::hash_range(value.begin(), value.end());
    }
};

template <>
struct hash<little_hash> {
    size_t operator()(little_hash const& value) const {
        return boost::hash_range(value.begin(), value.end());
    }
};

} // namspace std

data_chunk generate_random_bytes(std::default_random_engine& engine, size_t size) {
    data_chunk result(size);
    for (uint8_t& byte: result) {
        byte = engine() % std::numeric_limits<uint8_t>::max();
    }

    return result;
}

void create_database_file() {
    constexpr size_t header_size = slab_hash_table_header_size(buckets);

    store::create(DIRECTORY "/slab_hash_table__write_read");
    memory_map file(DIRECTORY "/slab_hash_table__write_read");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
    file.resize(header_size + minimum_slabs_size);

    slab_hash_table_header header(file, buckets);
    REQUIRE(header.create());
    REQUIRE(header.start());

    file_offset const slab_start = header_size;

    slab_manager alloc(file, slab_start);
    REQUIRE(alloc.create());
    REQUIRE(alloc.start());

    slab_hash_table<hash_digest> ht(header, alloc);

    std::default_random_engine engine;
    for (size_t i = 0; i < total_txs; ++i) {
        data_chunk value = generate_random_bytes(engine, tx_size);
        hash_digest key = bitcoin_hash(value);
        auto write = [&value](serializer<uint8_t*>& serial)
        {
            serial.write_forward(value);
        };
        ht.store(key, write, value.size());
    }

    alloc.sync();
}

class hash_table_directory_setup_fixture {
public:
    hash_table_directory_setup_fixture() {
        std::error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));
    }

    ////~hash_table_directory_setup_fixture() {
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////}
};

BOOST_FIXTURE_TEST_SUITE(hash_table_tests, hash_table_directory_setup_fixture)

TEST_CASE("slab hash table  write read  test", "[None]") {
    // Create the data file to be read below.
    create_database_file();

    memory_map file(DIRECTORY "/slab_hash_table__write_read");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);

    slab_hash_table_header header(file, buckets);
    REQUIRE(header.start());
    REQUIRE(header.size() == buckets);

    auto const slab_start = slab_hash_table_header_size(buckets);

    slab_manager alloc(file, slab_start);
    REQUIRE(alloc.start());

    slab_hash_table<hash_digest> ht(header, alloc);

    std::default_random_engine engine;
    for (size_t i = 0; i < total_txs; ++i) {
        auto const value = generate_random_bytes(engine, tx_size);
        auto const key = bitcoin_hash(value);
        auto const memory = ht.find(key);
        auto const slab = REMAP_ADDRESS(memory);

        REQUIRE(slab);
        REQUIRE(std::equal(value.begin(), value.end(), slab));
    }
}

TEST_CASE("slab hash table  test", "[None]") {
    store::create(DIRECTORY "/slab_hash_table");
    memory_map file(DIRECTORY "/slab_hash_table");
    REQUIRE(file.open());
    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
    file.resize(4 + 8 * 100 + 8);

    slab_hash_table_header header(file, 100);
    REQUIRE(header.create());
    REQUIRE(header.start());

    slab_manager alloc(file, 4 + 8 * 100);
    REQUIRE(alloc.create());
    REQUIRE(alloc.start());

    slab_hash_table<tiny_hash> ht(header, alloc);
    auto const write = [](serializer<uint8_t*>& serial) {
        serial.write_byte(110);
        serial.write_byte(110);
        serial.write_byte(4);
        serial.write_byte(99);
    };
    ht.store(tiny_hash{ { 0xde, 0xad, 0xbe, 0xef } }, write, 8);
    auto const memory1 = ht.find(tiny_hash{ { 0xde, 0xad, 0xbe, 0xef } });
    auto const slab1 = REMAP_ADDRESS(memory1);
    REQUIRE(slab1);
    REQUIRE(slab1[0] == 110);
    REQUIRE(slab1[1] == 110);
    REQUIRE(slab1[2] == 4);
    REQUIRE(slab1[3] == 99);

    auto const memory2 = ht.find(tiny_hash{ { 0xde, 0xad, 0xbe, 0xee } });
    auto const slab2 = REMAP_ADDRESS(memory1);
    REQUIRE(slab2);
}

////TEST_CASE("record hash table  32bit  test", "[None]")
////{
////    constexpr size_t record_buckets = 2;
////    constexpr size_t header_size = record_hash_table_header_size(record_buckets);
////
////    store::create(DIRECTORY "/record_hash_table__32bit");
////    memory_map file(DIRECTORY "/record_hash_table__32bit");
////    REQUIRE(file.open());
////
////    // Cannot hold an address reference because of following resize operation.
////    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
////    file.resize(header_size + minimum_records_size);
////
////    record_hash_table_header header(file, record_buckets);
////    REQUIRE(header.create());
////    REQUIRE(header.start());
////
////    typedef byte_array<4> tiny_hash;
////    constexpr size_t record_size = hash_table_record_size<tiny_hash>(4);
////    const file_offset records_start = header_size;
////
////    record_manager alloc(file, records_start, record_size);
////    REQUIRE(alloc.create());
////    REQUIRE(alloc.start());
////
////    record_hash_table<tiny_hash> ht(header, alloc);
////    tiny_hash key{ { 0xde, 0xad, 0xbe, 0xef } };
////    tiny_hash key1{ { 0xb0, 0x0b, 0xb0, 0x0b } };
////
////    auto const write = [](serializer<uint8_t*>& serial)
////    {
////        serial.write_byte(110);
////        serial.write_byte(110);
////        serial.write_byte(4);
////        serial.write_byte(88);
////    };
////
////    auto const write1 = [](serializer<uint8_t*>& serial)
////    {
////        serial.write_byte(99);
////        serial.write_byte(98);
////        serial.write_byte(97);
////        serial.write_byte(96);
////    };
////
////    // [e][e]
////    REQUIRE(header.read(0) == header.empty);
////    REQUIRE(header.read(1) == header.empty);
////
////    ht.store(key, write);
////    alloc.sync();
////
////    // [0][e]
////    REQUIRE(header.read(0) == 0u);
////    REQUIRE(header.read(1) == header.empty);
////
////    ht.store(key, write);
////    alloc.sync();
////
////    // [1->0][e]
////    REQUIRE(header.read(0) == 1u);
////
////    ht.store(key1, write1);
////    alloc.sync();
////
////    // [1->0][2]
////    REQUIRE(header.read(0) == 1u);
////    REQUIRE(header.read(1) == 2u);
////
////    ht.store(key1, write);
////    alloc.sync();
////
////    // [1->0][3->2]
////    REQUIRE(header.read(0) == 1u);
////    REQUIRE(header.read(1) == 3u);
////
////    // Verify 0->empty
////    record_row<tiny_hash> item0(alloc, 0);
////    REQUIRE(item0.next_index() == header.empty);
////
////    // Verify 1->0
////    record_row<tiny_hash> item1(alloc, 1);
////    REQUIRE(item1.next_index() == 0u);
////
////    // Verify 2->empty
////    record_row<tiny_hash> item2(alloc, 2);
////    REQUIRE(item2.next_index() == header.empty);
////
////    // Verify 3->2
////    record_row<tiny_hash> item3(alloc, 3);
////    REQUIRE(item3.next_index() == 2u);
////
////    // [X->0][3->2]
////    REQUIRE(ht.unlink(key));
////    alloc.sync();
////
////    REQUIRE(header.read(0) == 0);
////    REQUIRE(header.read(1) == 3u);
////
////    // Verify 0->empty
////    record_row<tiny_hash> item0a(alloc, 0);
////    REQUIRE(item0a.next_index() == header.empty);
////
////    // Verify 3->2
////    record_row<tiny_hash> item3a(alloc, 3);
////    REQUIRE(item3a.next_index() == 2u);
////
////    // Verify 2->empty
////    record_row<tiny_hash> item2a(alloc, 2);
////    REQUIRE(item2a.next_index() == header.empty);
////
////    // [0][X->2]
////    REQUIRE(ht.unlink(key1));
////    alloc.sync();
////
////    REQUIRE(header.read(0) == 0u);
////    REQUIRE(header.read(1) == 2u);
////
////    // Verify 0->empty
////    record_row<tiny_hash> item0b(alloc, 0);
////    REQUIRE(item0b.next_index() == header.empty);
////
////    // Verify 2->empty
////    record_row<tiny_hash> item2b(alloc, 2);
////    REQUIRE(item2b.next_index() == header.empty);
////
////    tiny_hash invalid{ { 0x00, 0x01, 0x02, 0x03 } };
////    REQUIRE( ! ht.unlink(invalid));
////}

////TEST_CASE("record hash table  64bit  test", "[None]")
////{
////    constexpr size_t record_buckets = 2;
////    constexpr size_t header_size = record_hash_table_header_size(record_buckets);
////
////    store::create(DIRECTORY "/record_hash_table_64bit");
////    memory_map file(DIRECTORY "/record_hash_table_64bit");
////    REQUIRE(file.open());
////
////    // Cannot hold an address reference because of following resize operation.
////    REQUIRE(REMAP_ADDRESS(file.access()) != nullptr);
////    file.resize(header_size + minimum_records_size);
////
////    record_hash_table_header header(file, record_buckets);
////    REQUIRE(header.create());
////    REQUIRE(header.start());
////
////    typedef byte_array<8> little_hash;
////    constexpr size_t record_size = hash_table_record_size<little_hash>(8);
////    const file_offset records_start = header_size;
////
////    record_manager alloc(file, records_start, record_size);
////    REQUIRE(alloc.create());
////    REQUIRE(alloc.start());
////
////    record_hash_table<little_hash> ht(header, alloc);
////
////    little_hash key{ { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef } };
////    little_hash key1{ { 0xb0, 0x0b, 0xb0, 0x0b, 0xb0, 0x0b, 0xb0, 0x0b } };
////
////    auto const write = [](serializer<uint8_t*>& serial)
////    {
////        serial.write_byte(110);
////        serial.write_byte(110);
////        serial.write_byte(4);
////        serial.write_byte(88);
////        serial.write_byte(110);
////        serial.write_byte(110);
////        serial.write_byte(4);
////        serial.write_byte(88);
////    };
////
////    auto const write1 = [](serializer<uint8_t*>& serial)
////    {
////        serial.write_byte(99);
////        serial.write_byte(98);
////        serial.write_byte(97);
////        serial.write_byte(96);
////        serial.write_byte(95);
////        serial.write_byte(94);
////        serial.write_byte(93);
////        serial.write_byte(92);
////    };
////
////    ht.store(key, write);
////    alloc.sync();
////
////    // [e][0]
////    REQUIRE(header.read(0) == header.empty);
////    REQUIRE(header.read(1) == 0u);
////
////    ht.store(key, write);
////    alloc.sync();
////
////    // [e][1->0]
////    REQUIRE(header.read(0) == header.empty);
////    REQUIRE(header.read(1) == 1u);
////
////    ht.store(key1, write1);
////    alloc.sync();
////
////    // [2][1->0]
////    REQUIRE(header.read(0) == 2u);
////    REQUIRE(header.read(1) == 1u);
////
////    ht.store(key1, write);
////    alloc.sync();
////
////    // [3->2][1->0]
////    REQUIRE(header.read(0) == 3u);
////    REQUIRE(header.read(1) == 1u);
////
////    record_row<little_hash> item(alloc, 3);
////    REQUIRE(item.next_index() == 2u);
////
////    record_row<little_hash> item1(alloc, 2);
////    REQUIRE(item1.next_index() == header.empty);
////
////    // [3->2][X->0]
////    REQUIRE(ht.unlink(key));
////    alloc.sync();
////
////    REQUIRE(header.read(1) == 0u);
////
////    // [X->2][X->0]
////    REQUIRE(ht.unlink(key1));
////    alloc.sync();
////
////    REQUIRE(header.read(0) == 2u);
////
////    little_hash invalid{ { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 } };
////    REQUIRE( ! ht.unlink(invalid));
////}

// End Test Suite

