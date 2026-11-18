// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/node/wallet/filter_store.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
namespace wallet = kth::node::wallet;

namespace {

struct temporary_directory {
    std::filesystem::path path;
    explicit temporary_directory(std::string_view tag)
        : path(std::filesystem::temp_directory_path() / (std::string("kth_wallet_") + std::string(tag))) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }
    ~temporary_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

data_chunk chunk_of(std::string_view hex) {
    auto decoded = decode_base16(hex);
    REQUIRE(decoded);
    return std::move(*decoded);
}

hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0x5a};
    return sha256_hash(bytes);
}

std::vector<wallet::filter_entry> chain_of_filters(std::vector<hash_digest> const& blocks,
        std::vector<data_chunk> const& filters, hash_digest previous = null_hash) {
    std::vector<wallet::filter_entry> entries;
    for (size_t i = 0; i < blocks.size(); ++i) {
        previous = wallet::next_filter_header(filters[i], previous);
        entries.push_back(wallet::filter_entry{blocks[i], previous, filters[i]});
    }
    return entries;
}

} // namespace

// Filter store
// ----------------------------------------------------------------------------

TEST_CASE("wallet filter store appends, reopens, and keeps only the header chain's blocks", "[wallet]") {
    temporary_directory dir("store_reopen");
    std::vector<hash_digest> blocks;
    std::vector<data_chunk> filters;
    for (uint32_t i = 0; i < 5; ++i) {
        blocks.push_back(digest_of(i));
        filters.push_back(data_chunk(40 + i, uint8_t(i)));
    }
    auto const entries = chain_of_filters(blocks, filters);
    auto const lookup = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < blocks.size()) return blocks[height];
        return std::nullopt;
    };
    {
        auto store = wallet::filter_store::open(dir.path, 1, lookup);
        REQUIRE(store);
        REQUIRE(store->empty());
        REQUIRE( ! store->tip());
        REQUIRE(store->tip_header() == null_hash);
        REQUIRE( ! store->append(std::span(entries).first(3)));
        REQUIRE( ! store->append(std::span(entries).subspan(3)));
        REQUIRE(store->tip() == 4u);
        REQUIRE(*store->read(4) == filters[4]);
        REQUIRE(store->data_bytes() == 40u + 41 + 42 + 43 + 44);
        REQUIRE(store->header(2) == entries[2].header);
        REQUIRE(store->block(3) == blocks[3]);
        REQUIRE( ! store->header(5));
        REQUIRE(store->read(5).error() == error::not_found);
        auto const where = store->locations(1, 3);
        REQUIRE(where.size() == 2u);
        REQUIRE(where[0].height == 1u);
        REQUIRE(where[0].offset == 40u);
        REQUIRE(where[1].length == 42u);
    }
    {
        auto const store = wallet::filter_store::open(dir.path, 1, lookup);
        REQUIRE(store);
        REQUIRE(store->tip() == 4u);
        REQUIRE(*store->read(0) == filters[0]);
    }
    // A header chain that changed at height 3 keeps heights 0 to 2.
    auto changed = blocks;
    changed[3] = digest_of(99);
    auto const other = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < changed.size()) return changed[height];
        return std::nullopt;
    };
    auto store = wallet::filter_store::open(dir.path, 1, other);
    REQUIRE(store);
    REQUIRE(store->tip() == 2u);
    REQUIRE(*store->read(2) == filters[2]);
    REQUIRE(store->tip_header() == entries[2].header);

    // Each filter type has files of its own.
    auto const second = wallet::filter_store::open(dir.path, 2, lookup);
    REQUIRE(second);
    REQUIRE(second->empty());
}

TEST_CASE("wallet filter store drops a torn write and refuses a damaged filter", "[wallet]") {
    temporary_directory dir("store_damage");
    std::vector<hash_digest> blocks;
    std::vector<data_chunk> filters;
    for (uint32_t i = 0; i < 20; ++i) {
        blocks.push_back(digest_of(1000 + i));
        filters.push_back(data_chunk(30, uint8_t(i + 1)));
    }
    auto const entries = chain_of_filters(blocks, filters);
    auto const lookup = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < blocks.size()) return blocks[height];
        return std::nullopt;
    };
    std::filesystem::path data;
    {
        auto store = wallet::filter_store::open(dir.path, 1, lookup);
        REQUIRE(store);
        REQUIRE( ! store->append(entries));
        data = store->data_path();
    }
    // The last filter loses its last byte, as a write cut short would leave it.
    std::filesystem::resize_file(data, std::filesystem::file_size(data) - 1);
    {
        auto const store = wallet::filter_store::open(dir.path, 1, lookup);
        REQUIRE(store);
        REQUIRE(store->tip() == 18u);
    }
    // A changed byte in the first filter, beyond the newest sixteen checked on
    // open, is found when the filter is read.
    {
        std::fstream file(data, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(5);
        file.put(char(0x7f));
    }
    auto store = wallet::filter_store::open(dir.path, 1, lookup);
    REQUIRE(store);
    REQUIRE(store->tip() == 18u);
    REQUIRE(store->read(0).error() == error::bad_stream);
    REQUIRE(*store->read(1) == filters[1]);

    // A newer filter changed in place is dropped on open with what follows it.
    {
        std::fstream file(data, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(30 * 10 + 3);
        file.put(char(0x7f));
    }
    auto const cut = wallet::filter_store::open(dir.path, 1, lookup);
    REQUIRE(cut);
    REQUIRE(cut->tip() == 9u);
}

TEST_CASE("wallet filter store starts at a birthday after a given filter header", "[wallet]") {
    temporary_directory dir("store_birthday");
    std::vector<hash_digest> blocks;
    for (uint32_t i = 0; i < 1010; ++i) blocks.push_back(digest_of(5000 + i));
    auto const lookup = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < blocks.size()) return blocks[height];
        return std::nullopt;
    };
    auto const before = digest_of(77);
    std::vector<hash_digest> later(blocks.begin() + 1000, blocks.end());
    std::vector<data_chunk> filters(later.size(), data_chunk{0x01, 0x02, 0x03});
    auto const entries = chain_of_filters(later, filters, before);
    {
        auto store = wallet::filter_store::open(dir.path, 2, lookup);
        REQUIRE(store);
        REQUIRE( ! store->restart(1000, before));
        REQUIRE(store->first() == 1000u);
        REQUIRE(store->next() == 1000u);
        REQUIRE(store->tip_header() == before);
        REQUIRE( ! store->append(entries));
        REQUIRE(store->tip() == 1009u);
        REQUIRE( ! store->header(999));
        REQUIRE(store->read(999).error() == error::not_found);
        REQUIRE(store->locations(0, 1003).size() == 3u);
    }
    auto store = wallet::filter_store::open(dir.path, 2, lookup);
    REQUIRE(store);
    REQUIRE(store->first() == 1000u);
    REQUIRE(store->previous_header() == before);
    REQUIRE(store->tip() == 1009u);
    REQUIRE(*store->read(1000) == filters[0]);
    // Cut below the first height empties it and keeps its start.
    REQUIRE( ! store->truncate(990));
    REQUIRE(store->empty());
    REQUIRE(store->first() == 1000u);
    REQUIRE(store->tip_header() == before);
}

TEST_CASE("wallet filter store refuses a folder it cannot create", "[wallet]") {
    temporary_directory dir("store_refused");
    auto const blocker = dir.path / "file";
    std::ofstream(blocker) << "not a folder";
    auto const store = wallet::filter_store::open(blocker / "inside", 1,
        [](uint32_t) -> std::optional<hash_digest> { return std::nullopt; });
    REQUIRE( ! store);
    REQUIRE(store.error() == error::file_system);
}

TEST_CASE("wallet filter header chains a filter hash to the previous header", "[wallet]") {
    // Block 127 of the bchd simnet vectors (txid-vectors.md): its served
    // filter and the header it chains to.
    auto const served = chunk_of("0203471b91863237e77edce7baf8aece3399215b146501a2d5da70a0cc33819107e621fae1832906b2f0e54a00d93660c5896e");
    auto const parent = chunk_of("3362679fcffacc8c327f41e9e79b3818bcca4f2359438ca3270350ea39b64ad1");
    hash_digest previous{};
    std::copy(parent.begin(), parent.end(), previous.begin());
    REQUIRE(encode_base16(wallet::next_filter_header(served, previous)) ==
        "e5058dd1d7d008d1391eec4ec1abc95748c837760f02c0157b4b6357d925839b");
    REQUIRE(wallet::next_filter_header_from_hash(bitcoin_hash(served), previous) ==
        wallet::next_filter_header(served, previous));
}
