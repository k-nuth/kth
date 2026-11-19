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

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/golomb_set.hpp>
#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/node/wallet/blocks.hpp>
#include <kth/node/wallet/filter_store.hpp>
#include <kth/node/wallet/history.hpp>
#include <kth/node/wallet/scan.hpp>

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

chain::script script_of(data_chunk bytes) {
    return chain::script(bytes, false);
}

chain::transaction coinbase_paying(data_chunk const& script, uint64_t value, uint32_t salt) {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index},
        script_of({0x01, uint8_t(salt), uint8_t(salt >> 8)}), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(value, script_of(script), chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

struct paid {
    uint64_t value;
    data_chunk script;
    chain::token_data_opt token{};
};

chain::transaction spending(std::vector<chain::point> const& previous, std::vector<paid> const& outputs, uint32_t salt) {
    chain::input::list inputs;
    for (auto const& point : previous) {
        inputs.emplace_back(chain::output_point{point.hash(), point.index()}, script_of({0x01, uint8_t(salt)}), 0xffffffff);
    }
    chain::output::list made;
    for (auto const& out : outputs) {
        made.emplace_back(out.value, script_of(out.script), out.token);
    }
    return chain::transaction(2, 0, std::move(inputs), std::move(made));
}

// A served script filter (root, then the set) holding `keys`, for `block`.
data_chunk script_filter_of(hash_digest const& block, std::vector<hash_digest> const& keys) {
    std::vector<data_chunk> items;
    for (auto const& key : keys) items.push_back(to_chunk(key));
    auto const set = chain::golomb_set::build(chain::compact_filter_key(block), items);
    REQUIRE(set);
    data_chunk served(hash_size, 0x42);
    auto const bytes = set->to_data();
    served.insert(served.end(), bytes.begin(), bytes.end());
    return served;
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

// Static data: decoded without assertions, which belong to test cases.
data_chunk decoded(std::string_view hex) {
    auto bytes = decode_base16(hex);
    return bytes ? std::move(*bytes) : data_chunk{};
}

data_chunk const p2pkh_a = decoded("76a914000102030405060708090a0b0c0d0e0f1011121388ac");
data_chunk const p2pkh_b = decoded("76a914aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa88ac");
data_chunk const p2sh_c = decoded("a914cccccccccccccccccccccccccccccccccccccccc87");

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

// Scan
// ----------------------------------------------------------------------------

TEST_CASE("wallet scan finds each key in its blocks from its own height, on any number of threads", "[wallet]") {
    temporary_directory dir("scan_keys");
    std::vector<hash_digest> watched;
    for (uint32_t i = 0; i < 4; ++i) watched.push_back(digest_of(90000 + i));
    std::vector<hash_digest> blocks;
    std::vector<data_chunk> filters;
    for (uint32_t height = 0; height < 200; ++height) {
        auto const block = digest_of(height * 7 + 3);
        std::vector<hash_digest> keys;
        for (uint32_t j = 0; j < 50; ++j) keys.push_back(digest_of(height * 1000 + j + 100000));
        // Key i is in the blocks whose height is a multiple of i + 2.
        for (uint32_t i = 0; i < watched.size(); ++i) {
            if (height % (i + 2) == 0) keys.push_back(watched[i]);
        }
        blocks.push_back(block);
        filters.push_back(script_filter_of(block, keys));
    }
    // A filter too short to hold a root matches every key.
    filters[150] = data_chunk(10, 0);
    auto const lookup = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < blocks.size()) return blocks[height];
        return std::nullopt;
    };
    auto store = wallet::filter_store::open(dir.path, 1, lookup);
    REQUIRE(store);
    REQUIRE( ! store->append(chain_of_filters(blocks, filters)));
    std::vector<wallet::scan_key> keys;
    for (uint32_t i = 0; i < watched.size(); ++i) keys.push_back({watched[i], i * 10});
    auto const where = store->locations(0, 200);
    for (size_t threads : {1, 3, 8}) {
        auto const hits = wallet::scan_script_filters(store->data_path(), where, keys, threads);
        REQUIRE(hits);
        REQUIRE(std::ranges::is_sorted(*hits, {}, [](auto const& hit) { return hit.first; }));
        for (uint32_t i = 0; i < watched.size(); ++i) {
            std::vector<uint32_t> found;
            for (auto const& [height, matched] : *hits) {
                if (std::ranges::find(matched, size_t(i)) != matched.end()) found.push_back(height);
            }
            std::vector<uint32_t> expected;
            for (uint32_t height = i * 10; height < 200; ++height) {
                if (height % (i + 2) == 0 || height == 150) expected.push_back(height);
            }
            // A false positive is possible, at 1 in 784931 per block and key.
            for (auto const height : expected) {
                REQUIRE(std::ranges::find(found, height) != found.end());
            }
            REQUIRE(found.size() <= expected.size() + 1);
        }
    }
    // Nothing to scan.
    REQUIRE(wallet::scan_script_filters(store->data_path(), {}, keys, 2)->empty());
    REQUIRE(wallet::scan_script_filters(store->data_path(), where, {}, 2)->empty());
    // A file that is gone.
    auto const missing = wallet::scan_script_filters(dir.path / "missing.dat", where, keys, 1);
    REQUIRE( ! missing);
    REQUIRE(missing.error() == error::file_system);
}

TEST_CASE("wallet scan matches input prefixes against input prefix filters", "[wallet]") {
    temporary_directory dir("scan_prefixes");
    std::vector<hash_digest> blocks;
    std::vector<data_chunk> filters;
    for (uint32_t height = 0; height < 40; ++height) {
        std::vector<uint16_t> values{uint16_t(height * 3), 0xa5a5};
        if (height % 4 == 0) values.push_back(0x1234);
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
        blocks.push_back(digest_of(700 + height));
        filters.push_back(chain::to_data(chain::input_prefix_filter{values}));
    }
    filters[33] = data_chunk{0x02, 0x00};
    auto const lookup = [&](uint32_t height) -> std::optional<hash_digest> {
        if (height < blocks.size()) return blocks[height];
        return std::nullopt;
    };
    auto store = wallet::filter_store::open(dir.path, 2, lookup);
    REQUIRE(store);
    REQUIRE( ! store->append(chain_of_filters(blocks, filters)));
    std::vector<wallet::scan_prefix> prefixes{{0x12, 8, 0}, {0x1234, 16, 10}, {0xa, 4, 39}, {0x7777, 16, 0}};
    auto const hits = wallet::scan_input_prefix_filters(store->data_path(), store->locations(0, 40), prefixes, 2);
    REQUIRE(hits);
    auto const heights_of = [&](size_t k) {
        std::vector<uint32_t> out;
        for (auto const& [height, matched] : *hits) {
            if (std::ranges::find(matched, k) != matched.end()) out.push_back(height);
        }
        return out;
    };
    std::vector<uint32_t> byte_prefix;
    std::vector<uint32_t> whole;
    for (uint32_t height = 0; height < 40; ++height) {
        if (height % 4 == 0 || height == 33 || (height * 3) >> 8 == 0x12) byte_prefix.push_back(height);
        if (height >= 10 && (height % 4 == 0 || height == 33)) whole.push_back(height);
    }
    REQUIRE(heights_of(0) == byte_prefix);
    REQUIRE(heights_of(1) == whole);
    REQUIRE(heights_of(2) == std::vector<uint32_t>{39});
    REQUIRE(heights_of(3) == std::vector<uint32_t>{33});
}

TEST_CASE("wallet script filter match returns every key when the filter does not parse", "[wallet]") {
    auto const block = digest_of(4);
    std::vector<hash_digest> keys{digest_of(1), digest_of(2), digest_of(3)};
    auto const filter = script_filter_of(block, {digest_of(2), digest_of(9)});
    REQUIRE(wallet::match_script_filter(block, filter, keys) == std::vector<size_t>{1});
    REQUIRE(wallet::match_script_filter(block, data_chunk(32, 0), keys) == std::vector<size_t>{0, 1, 2});
    auto truncated = filter;
    truncated.resize(hash_size + 2);
    REQUIRE(wallet::match_script_filter(block, truncated, keys).size() == 3u);
}

// Histories
// ----------------------------------------------------------------------------

TEST_CASE("wallet history finds outputs and spends within and across blocks, and rolls back", "[wallet]") {
    wallet::target_state state(wallet::target::of_script(p2pkh_a), 0);
    REQUIRE(state.next_height() == 0u);
    auto const coinbase = coinbase_paying(p2pkh_a, 5000, 1);
    REQUIRE(state.apply_block(1, digest_of(1), {coinbase}));
    REQUIRE(state.synced() == 1u);

    // Block 2 spends the coinbase output and pays the script again, and a
    // transaction earlier in the block (canonical order) spends that output.
    auto const spend = spending({chain::point(coinbase.hash(), 0)}, {{4000, p2pkh_a}, {900, p2pkh_b}}, 2);
    auto const respend = spending({chain::point(spend.hash(), 0)}, {{3900, p2pkh_b}}, 3);
    auto const unrelated = coinbase_paying(p2pkh_b, 1, 4);
    REQUIRE(state.apply_block(2, digest_of(2), {unrelated, respend, spend}));
    REQUIRE(state.history().size() == 3u);
    REQUIRE(state.unspent().empty());
    REQUIRE(state.history()[1].txid == respend.hash());
    REQUIRE(state.history()[1].position == 1u);
    REQUIRE(state.history()[1].inputs.size() == 1u);
    REQUIRE(state.history()[1].inputs[0].value == 4000u);
    REQUIRE(state.history()[2].outputs[0].value == 4000u);
    REQUIRE(state.history()[2].inputs[0].value == 5000u);
    REQUIRE(state.is_spent(chain::point(coinbase.hash(), 0)));

    // A block that touches nothing changes nothing but the height.
    REQUIRE( ! state.apply_block(3, digest_of(3), {coinbase_paying(p2pkh_b, 1, 5)}));
    REQUIRE(state.synced() == 3u);

    REQUIRE(state.rollback(1));
    REQUIRE(state.synced() == 1u);
    REQUIRE(state.history().size() == 1u);
    REQUIRE(state.unspent().size() == 1u);
    REQUIRE( ! state.is_spent(chain::point(coinbase.hash(), 0)));
    REQUIRE( ! state.rollback(5));
}

TEST_CASE("wallet history follows a token category across scripts, and filters tokens", "[wallet]") {
    auto const category = digest_of(31337);
    auto const nft = chain::make_non_fungible(category, chain::capability_t::mut, data_chunk{0x01});
    wallet::target_state state(wallet::target::of_category(category), 5);
    REQUIRE(state.next_height() == 5u);
    auto const mint = spending({chain::point(digest_of(3), 0)}, {{1000, p2sh_c, nft}, {500, p2sh_c}}, 1);
    REQUIRE(state.apply_block(5, digest_of(5), {coinbase_paying(p2pkh_b, 1, 9), mint}));
    auto const moved = spending({chain::point(mint.hash(), 0)}, {{900, p2pkh_b, nft}}, 2);
    REQUIRE(state.apply_block(6, digest_of(6), {coinbase_paying(p2pkh_b, 1, 10), moved}));
    auto const unspent = state.unspent();
    REQUIRE(unspent.size() == 1u);
    REQUIRE(unspent[0].outpoint == chain::point(moved.hash(), 0));
    REQUIRE(unspent[0].script == p2pkh_b);
    REQUIRE(unspent[0].token == chain::token_data_opt{nft});
    REQUIRE(state.history().size() == 2u);

    REQUIRE(wallet::keeps(wallet::token_filter::include, unspent[0].token));
    REQUIRE(wallet::keeps(wallet::token_filter::only, unspent[0].token));
    REQUIRE( ! wallet::keeps(wallet::token_filter::exclude, unspent[0].token));
    REQUIRE(wallet::keeps(wallet::token_filter::exclude, chain::token_data_opt{}));
    REQUIRE( ! wallet::keeps(wallet::token_filter::only, chain::token_data_opt{}));

    // A rollback below the birthday leaves it as if never synced.
    REQUIRE(state.rollback(3));
    REQUIRE( ! state.synced());
    REQUIRE(state.next_height() == 5u);
}

TEST_CASE("wallet unspent outputs are in chain order, as Fulcrum lists them", "[wallet]") {
    wallet::target_state state(wallet::target::of_script(p2pkh_a), 0);
    // Two payments in one block: listed by their position in it, whatever their hashes.
    auto const first = spending({chain::point(digest_of(41), 0)}, {{10, p2pkh_a}}, 41);
    auto const second = spending({chain::point(digest_of(42), 0)}, {{20, p2pkh_a}, {30, p2pkh_a}}, 42);
    REQUIRE(state.apply_block(3, digest_of(3), {coinbase_paying(p2pkh_b, 1, 3), second, first}));
    auto const listed = state.unspent();
    REQUIRE(listed.size() == 3u);
    REQUIRE(listed[0].outpoint == chain::point(second.hash(), 0));
    REQUIRE(listed[1].outpoint == chain::point(second.hash(), 1));
    REQUIRE(listed[2].outpoint == chain::point(first.hash(), 0));
    REQUIRE(listed[2].position == 2u);
}

TEST_CASE("wallet targets have the script filter keys of what they watch", "[wallet]") {
    auto const by_script = wallet::target::of_script(p2pkh_a);
    auto const by_hash = wallet::target::of_script_hash(sha256_hash(p2pkh_a));
    REQUIRE(by_script.key() == chain::script_key(p2pkh_a));
    REQUIRE(by_hash.key() == by_script.key());
    auto const category = digest_of(8);
    REQUIRE(wallet::target::of_category(category).key() == chain::category_key(category));
    chain::output const out(10, script_of(p2pkh_a), chain::token_data_opt{});
    REQUIRE(by_script.owns(out));
    REQUIRE(by_hash.owns(out));
    REQUIRE( ! wallet::target::of_script(p2pkh_b).owns(out));
    REQUIRE( ! wallet::target::of_category(category).owns(out));
    REQUIRE(by_script != by_hash);
}

// Mempool copy
// ----------------------------------------------------------------------------

TEST_CASE("wallet mempool copy knows fees, chains, and what a block removes", "[wallet]") {
    wallet::mempool_copy pool(1 << 20);
    chain::point const funding(digest_of(1), 0);
    auto const parent = spending({funding}, {{1000, p2pkh_a}}, 1);
    auto const child = spending({chain::point(parent.hash(), 0)}, {{800, p2pkh_b}}, 2);
    auto const known = [&](chain::point const& point) -> std::optional<uint64_t> {
        if (point == funding) return 1500;
        return std::nullopt;
    };
    REQUIRE(pool.insert(parent, known));
    REQUIRE( ! pool.insert(parent, known));
    REQUIRE(pool.insert(child, nullptr));
    REQUIRE( ! pool.insert(coinbase_paying(p2pkh_a, 1, 1), known));
    REQUIRE(pool.find(parent.hash())->fee == 500u);
    REQUIRE(pool.find(child.hash())->fee == 200u);
    REQUIRE(pool.has_unconfirmed_parent(child));
    REQUIRE( ! pool.has_unconfirmed_parent(parent));
    REQUIRE(pool.spender(funding) == parent.hash());
    REQUIRE(pool.size() == 2u);

    // An unknown input leaves the fee unknown.
    auto const stranger = spending({chain::point(digest_of(2), 3)}, {{10, p2pkh_a}}, 3);
    REQUIRE(pool.insert(stranger, known));
    REQUIRE( ! pool.find(stranger.hash())->fee);

    // A block that spends the funding output another way removes the parent
    // and its child, and one that confirms a transaction removes it.
    auto const conflict = spending({funding}, {{1400, p2pkh_b}}, 4);
    auto const removed = pool.remove_for_block({coinbase_paying(p2pkh_b, 1, 2), conflict, stranger});
    REQUIRE(removed.size() == 3u);
    REQUIRE(pool.size() == 0u);
    REQUIRE(pool.bytes() == 0u);
    REQUIRE( ! pool.spender(funding));
}

TEST_CASE("wallet mempool copy drops the oldest transactions when full", "[wallet]") {
    auto const first = spending({chain::point(digest_of(1), 0)}, {{10, p2pkh_a}}, 1);
    auto const size = first.serialized_size(true);
    wallet::mempool_copy pool(size * 2);
    REQUIRE(pool.insert(first, nullptr));
    REQUIRE(pool.insert(spending({chain::point(digest_of(2), 0)}, {{10, p2pkh_a}}, 2), nullptr));
    REQUIRE(pool.insert(spending({chain::point(digest_of(3), 0)}, {{10, p2pkh_a}}, 3), nullptr));
    REQUIRE(pool.size() == 2u);
    REQUIRE( ! pool.contains(first.hash()));
    pool.clear();
    REQUIRE(pool.size() == 0u);
}
