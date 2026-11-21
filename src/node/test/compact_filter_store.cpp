// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/detail/compact_filter_store.hpp>

using namespace kth;
using node::detail::chain_filter_header;
using node::detail::compact_filter_record;
using node::detail::compact_filter_store;

namespace {

struct store_file {
    std::filesystem::path path;

    explicit store_file(char const* tag)
        : path(std::filesystem::temp_directory_path()
            / ("kth_cf_store_" + std::string(tag) + "_" + std::to_string(getpid())))
    {
        std::filesystem::remove(path);
    }

    ~store_file() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    data_chunk bytes() const {
        std::ifstream file(path, std::ios::binary);
        return data_chunk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    void write(data_chunk const& bytes) const {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
    }
};

// A hash no other test value has.
hash_digest hash_of(uint8_t first, uint8_t second = 0) {
    hash_digest hash{};
    hash[0] = first;
    hash[1] = second;
    hash[31] = 0x5a;
    return hash;
}

// `count` records, the filter hash of record i being hash_of(i).
compact_filter_store filled(std::filesystem::path const& path, size_t count) {
    compact_filter_store store(path);
    for (size_t i = 0; i < count; ++i) {
        store.append(hash_of(uint8_t(i)));
    }
    return store;
}

constexpr size_t header_size = compact_filter_store::file_header_size;
constexpr size_t record_size = compact_filter_store::record_size;

} // namespace

TEST_CASE("compact filter store chains each filter header to the one below", "[compact filters][store]") {
    store_file const file("chain");
    auto const store = filled(file.path, 3);
    REQUIRE(store.size() == 3);

    // The definition, spelled out: all zeros before the first block.
    auto const first = bitcoin_hash(splice(hash_of(0), null_hash));
    auto const second = bitcoin_hash(splice(hash_of(1), first));
    auto const third = bitcoin_hash(splice(hash_of(2), second));
    CHECK(store.at(0) == compact_filter_record{hash_of(0), first});
    CHECK(store.at(1) == compact_filter_record{hash_of(1), second});
    CHECK(store.at(2) == compact_filter_record{hash_of(2), third});
    CHECK(chain_filter_header(hash_of(1), first) == second);

    CHECK(store.header_before(0) == null_hash);
    CHECK(store.header_before(1) == first);
    CHECK(store.header_before(3) == third);
}

TEST_CASE("compact filter store reads back what it saved", "[compact filters][store]") {
    store_file const file("round_trip");
    auto store = filled(file.path, 5);
    CHECK( ! store.saved());
    REQUIRE( ! store.save(hash_of(0xb0)));
    CHECK(store.saved());
    CHECK(store.tip() == hash_of(0xb0));
    CHECK(file.bytes().size() == header_size + 5 * record_size);

    compact_filter_store loaded(file.path);
    loaded.load();
    REQUIRE(loaded.size() == 5);
    CHECK(loaded.saved());
    CHECK(loaded.tip() == hash_of(0xb0));
    for (size_t i = 0; i < 5; ++i) {
        CAPTURE(i);
        CHECK(loaded.at(i) == store.at(i));
    }
}

TEST_CASE("compact filter store with no file holds nothing", "[compact filters][store]") {
    store_file const file("missing");
    compact_filter_store store(file.path);
    store.load();
    CHECK(store.empty());
    CHECK(store.tip() == null_hash);
    CHECK(store.saved());
}

TEST_CASE("compact filter store adds to its file what a later save brings", "[compact filters][store]") {
    store_file const file("append");
    auto store = filled(file.path, 2);
    REQUIRE( ! store.save(hash_of(0xb0)));
    auto const before = file.bytes();

    store.append(hash_of(2));
    store.append(hash_of(3));
    CHECK( ! store.saved());
    REQUIRE( ! store.save(hash_of(0xb1)));

    // The records already there are the same bytes; the file is the one a
    // store that held all four from the start writes.
    auto const after = file.bytes();
    REQUIRE(after.size() == header_size + 4 * record_size);
    CHECK(std::equal(before.begin() + header_size, before.end(), after.begin() + header_size));

    store_file const whole_file("append_whole");
    auto whole = filled(whole_file.path, 4);
    REQUIRE( ! whole.save(hash_of(0xb1)));
    CHECK(after == whole_file.bytes());
}

TEST_CASE("compact filter store drops the records a reorganization left behind", "[compact filters][store]") {
    store_file const file("truncate");
    auto store = filled(file.path, 6);
    REQUIRE( ! store.save(hash_of(0xb0)));

    store.truncate(4);
    REQUIRE(store.size() == 4);
    CHECK( ! store.saved());
    // Another block at the height dropped, and one above it.
    store.append(hash_of(4, 1));
    store.append(hash_of(5, 1));
    REQUIRE( ! store.save(hash_of(0xb1)));

    compact_filter_store loaded(file.path);
    loaded.load();
    REQUIRE(loaded.size() == 6);
    CHECK(loaded.tip() == hash_of(0xb1));
    CHECK(loaded.at(3).filter_hash == hash_of(3));
    CHECK(loaded.at(4).filter_hash == hash_of(4, 1));
    CHECK(loaded.at(5) == store.at(5));

    // Shorter than before: the file is cut to what it counts.
    store.truncate(2);
    REQUIRE( ! store.save(hash_of(0xb2)));
    CHECK(file.bytes().size() == header_size + 2 * record_size);
    loaded.load();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded.tip() == hash_of(0xb2));

    // Down to nothing: no block is named.
    store.truncate(0);
    REQUIRE( ! store.save(hash_of(0xb3)));
    loaded.load();
    CHECK(loaded.empty());
    CHECK(loaded.tip() == null_hash);
}

TEST_CASE("compact filter store ignores what its file holds past its count", "[compact filters][store]") {
    store_file const file("past_count");
    auto store = filled(file.path, 3);
    REQUIRE( ! store.save(hash_of(0xb0)));

    // A save cut short after its records and before its count: the records
    // are there and the file still counts three.
    auto bytes = file.bytes();
    bytes.insert(bytes.end(), record_size + 7, uint8_t{0xee});
    file.write(bytes);

    compact_filter_store loaded(file.path);
    loaded.load();
    REQUIRE(loaded.size() == 3);
    CHECK(loaded.tip() == hash_of(0xb0));
    CHECK(loaded.at(2) == store.at(2));

    // The next save writes over them.
    loaded.append(hash_of(3));
    REQUIRE( ! loaded.save(hash_of(0xb1)));
    CHECK(file.bytes().size() == header_size + 4 * record_size);
    compact_filter_store again(file.path);
    again.load();
    REQUIRE(again.size() == 4);
    CHECK(again.at(3).filter_hash == hash_of(3));
}

TEST_CASE("compact filter store takes a file it cannot trust as holding nothing", "[compact filters][store]") {
    store_file const file("refused");
    auto store = filled(file.path, 4);
    REQUIRE( ! store.save(hash_of(0xb0)));
    auto const good = file.bytes();

    auto const loads_nothing = [&file](data_chunk const& bytes) {
        file.write(bytes);
        compact_filter_store loaded(file.path);
        loaded.load();
        return loaded.empty() && loaded.tip() == null_hash;
    };

    SECTION("the file as saved is taken") {
        CHECK( ! loads_nothing(good));
    }
    SECTION("another magic") {
        auto bytes = good;
        bytes[0] ^= 0xff;
        CHECK(loads_nothing(bytes));
    }
    SECTION("another version") {
        auto bytes = good;
        bytes[8] = 2;
        CHECK(loads_nothing(bytes));
    }
    SECTION("less than a header") {
        CHECK(loads_nothing(data_chunk(good.begin(), good.begin() + header_size - 1)));
    }
    SECTION("shorter than the records it counts") {
        CHECK(loads_nothing(data_chunk(good.begin(), good.end() - 1)));
    }
    SECTION("a filter hash changed") {
        auto bytes = good;
        bytes[header_size + 2 * record_size] ^= 0x01;
        CHECK(loads_nothing(bytes));
    }
    SECTION("a filter header changed") {
        auto bytes = good;
        bytes[header_size + record_size + hash_size] ^= 0x01;
        CHECK(loads_nothing(bytes));
    }

    // A store that took nothing writes its file from the start.
    file.write(data_chunk(good.begin(), good.end() - 1));
    compact_filter_store fresh(file.path);
    fresh.load();
    REQUIRE(fresh.empty());
    fresh.append(hash_of(9));
    REQUIRE( ! fresh.save(hash_of(0xb4)));
    CHECK(file.bytes().size() == header_size + record_size);
    compact_filter_store loaded(file.path);
    loaded.load();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded.at(0).filter_hash == hash_of(9));
}

TEST_CASE("compact filter store leaves its file whole when a save over counted records fails", "[compact filters][store]") {
    store_file const file("replace");
    auto store = filled(file.path, 6);
    REQUIRE( ! store.save(hash_of(0xb0)));
    auto const before = file.bytes();

    // A reorganization: other blocks from height 2 up, as many as before, so
    // every record the file counts above it would be written over.
    store.truncate(2);
    for (uint8_t i = 2; i < 6; ++i) {
        store.append(hash_of(i, 1));
    }

    // The save cannot write beside the file.
    auto beside = file.path;
    beside += ".new";
    std::filesystem::create_directory(beside);
    CHECK(store.save(hash_of(0xb1)));
    CHECK( ! store.saved());
    // The file is the one of the save before, byte for byte: it still loads
    // as that chain's.
    CHECK(file.bytes() == before);
    {
        compact_filter_store loaded(file.path);
        loaded.load();
        REQUIRE(loaded.size() == 6);
        CHECK(loaded.tip() == hash_of(0xb0));
        CHECK(loaded.at(5).filter_hash == hash_of(5));
    }

    // Tried again once it can: the file is the new chain's, and nothing is
    // left beside it.
    std::filesystem::remove(beside);
    REQUIRE( ! store.save(hash_of(0xb1)));
    CHECK(store.saved());
    CHECK( ! std::filesystem::exists(beside));
    compact_filter_store loaded(file.path);
    loaded.load();
    REQUIRE(loaded.size() == 6);
    CHECK(loaded.tip() == hash_of(0xb1));
    for (size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(loaded.at(i) == store.at(i));
    }
}
