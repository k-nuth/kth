// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace kth;
using namespace kd;

// The payloads below are the bytes a bchd node's wire package writes for the
// same messages (gcash/bchd#777), so the two nodes read each other.

namespace {

constexpr auto wire_version = message::version::level::maximum;

data_chunk bytes(std::string const& hex) {
    auto const result = decode_base16(hex);
    REQUIRE(result);
    return *result;
}

// Thirty-two bytes that count up from `first`.
hash_digest counting(uint8_t first) {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) hash[i] = uint8_t(first + i);
    return hash;
}

// The n hashes the vectors use: the i-th counts up from seed + 7 i.
std::vector<hash_digest> hashes(size_t n, uint8_t seed) {
    std::vector<hash_digest> out;
    for (size_t i = 0; i < n; ++i) out.push_back(counting(uint8_t(seed + 7 * i)));
    return out;
}

chain::script_leaf leaf(uint8_t first, uint32_t tx, uint32_t index, chain::script_leaf_kind kind) {
    return {counting(first), tx, index, kind};
}

chain::transaction parsed_transaction(std::string const& hex) {
    auto const raw = bytes(hex);
    byte_reader reader(raw);
    auto tx = chain::transaction::from_data(reader, true);
    REQUIRE(tx);
    REQUIRE(reader.is_exhausted());
    return std::move(*tx);
}

template <typename Message>
void check_boundaries(Message const& message) {
    auto const raw = to_data_chunk(message, wire_version);
    REQUIRE(raw.size() == message.serialized_size(wire_version));
    // No incomplete payload may parse, wherever it is cut, and no short buffer
    // may take the message.
    for (size_t size = 0; size < raw.size(); ++size) {
        INFO("payload prefix " << size);
        byte_reader reader(byte_span{raw.data(), size});
        REQUIRE( ! Message::from_data(reader, wire_version));
        data_chunk short_buffer(size);
        byte_writer writer(short_buffer);
        REQUIRE( ! message.to_data(writer, wire_version));
    }
    auto trailing = raw;
    trailing.push_back(0);
    REQUIRE( ! from_data_chunk<Message>(trailing, wire_version));
    auto const again = from_data_chunk<Message>(raw, wire_version);
    REQUIRE(again);
    REQUIRE(*again == message);
}

message::message_type type_of(std::string const& command) {
    return message::heading(0xe8f3e1e3, command, 0, 0).type();
}

auto const the_block = counting(0xb0);

using kind = chain::script_leaf_kind;

} // namespace

TEST_CASE("script filter tree messages carry their commands", "[script filters][message]") {
    REQUIRE(message::get_compact_filter_range::command == "getcfrange");
    REQUIRE(message::compact_filter_range::command == "cfrange");
    REQUIRE(message::get_compact_filter_proof::command == "getcfproof");
    REQUIRE(message::compact_filter_proof::command == "cfproof");

    REQUIRE(type_of("getcfrange") == message::message_type::get_compact_filter_range);
    REQUIRE(type_of("cfrange") == message::message_type::compact_filter_range);
    REQUIRE(type_of("getcfproof") == message::message_type::get_compact_filter_proof);
    REQUIRE(type_of("cfproof") == message::message_type::compact_filter_proof);
    REQUIRE(type_of("cfranges") == message::message_type::unknown);
    REQUIRE(type_of("getcfproofs") == message::message_type::unknown);

    REQUIRE(message::script_compact_filter == 1);
    REQUIRE(message::max_compact_filter_range_leaves == 33554432u / 41u);
}

TEST_CASE("getcfrange is a type, a block, a prefix length and a prefix", "[script filters][message]") {
    auto const raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf0c005ca00000000000000000000000"
        "00000000000000000000000000000000000000");
    REQUIRE(raw.size() == 67);
    auto const parsed = from_data_chunk<message::get_compact_filter_range>(raw, wire_version);
    REQUIRE(parsed);
    hash_digest prefix{};
    prefix[0] = 0x5c;
    prefix[1] = 0xa0;
    REQUIRE(*parsed == message::get_compact_filter_range{message::script_compact_filter, the_block, 12, prefix});
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);

    // No bits: every leaf.
    auto const all = from_data_chunk<message::get_compact_filter_range>(bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf000000000000000000000000000000"
        "00000000000000000000000000000000000000"), wire_version);
    REQUIRE(all);
    REQUIRE(*all == message::get_compact_filter_range{message::script_compact_filter, the_block, 0, null_hash});

    // A whole key is the longest prefix, and one bit more is refused both ways.
    auto longest = *parsed;
    longest.prefix_bits = 256;
    auto const longest_raw = to_data_chunk(longest, wire_version);
    REQUIRE(longest_raw.size() == 67);
    REQUIRE(from_data_chunk<message::get_compact_filter_range>(longest_raw, wire_version));
    auto over_raw = longest_raw;
    over_raw[33] = 0x01;   // 257, little-endian
    over_raw[34] = 0x01;
    REQUIRE( ! from_data_chunk<message::get_compact_filter_range>(over_raw, wire_version));
    auto over = *parsed;
    over.prefix_bits = 257;
    data_chunk buffer(over.serialized_size(wire_version));
    byte_writer writer(buffer);
    REQUIRE( ! over.to_data(writer, wire_version));

    // Bits set past the length parse: whether they are answered is the node's
    // answer, as for an unknown filter type.
    auto loose = raw;
    loose[66] = 0xff;
    REQUIRE(from_data_chunk<message::get_compact_filter_range>(loose, wire_version));
}

TEST_CASE("cfrange is the leaves with a prefix, their neighbours and the edge hashes", "[script filters][message]") {
    auto const raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf050000000100000002202122232425"
        "262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f010000000100000001303132333435363738393a3b3c"
        "3d3e3f404142434445464748494a4b4c4d4e4f020000000000000002101112131415161718191a1b1c1d1e1f20212223"
        "2425262728292a2b2c2d2e2f010000000000000000404142434445464748494a4b4c4d4e4f505152535455565758595a"
        "5b5c5d5e5f030000000200000003030102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2008"
        "090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526270f101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e");
    auto const parsed = from_data_chunk<message::compact_filter_range>(raw, wire_version);
    REQUIRE(parsed);
    message::compact_filter_range expected;
    expected.filter_type = message::script_compact_filter;
    expected.block_hash = the_block;
    expected.leaf_count = 5;
    expected.first_index = 1;
    expected.leaves = {leaf(0x20, 1, 1, kind::spent), leaf(0x30, 2, 0, kind::output_category)};
    expected.before = leaf(0x10, 1, 0, kind::output);
    expected.after = leaf(0x40, 3, 2, kind::spent_category);
    expected.hashes = hashes(3, 0x01);
    REQUIRE(*parsed == expected);
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);

    // No leaf has the prefix and one would stand first: only the leaf after.
    auto const none_raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf030000000000000000404142434445"
        "464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f030000000200000003022122232425262728292a2b2c"
        "2d2e2f303132333435363738393a3b3c3d3e3f4028292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40414243"
        "44454647");
    auto const none = from_data_chunk<message::compact_filter_range>(none_raw, wire_version);
    REQUIRE(none);
    REQUIRE(none->leaf_count == 3);
    REQUIRE(none->first_index == 0);
    REQUIRE(none->leaves.empty());
    REQUIRE( ! none->before);
    REQUIRE(none->after == leaf(0x40, 3, 2, kind::spent_category));
    REQUIRE(none->hashes == hashes(2, 0x21));
    REQUIRE(to_data_chunk(*none, wire_version) == none_raw);
    check_boundaries(*none);

    // Every leaf of the tree: no neighbour and no hash.
    auto const all_raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf020000000000000002202122232425"
        "262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f000000000000000000303132333435363738393a3b3c"
        "3d3e3f404142434445464748494a4b4c4d4e4f00000000010000000000");
    auto const all = from_data_chunk<message::compact_filter_range>(all_raw, wire_version);
    REQUIRE(all);
    REQUIRE(all->leaves == std::vector{leaf(0x20, 0, 0, kind::output), leaf(0x30, 0, 1, kind::output)});
    REQUIRE( ! all->before);
    REQUIRE( ! all->after);
    REQUIRE(all->hashes.empty());
    REQUIRE(to_data_chunk(*all, wire_version) == all_raw);

    // A tree of no leaves.
    auto const empty_raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf00000000000000000000");
    auto const empty = from_data_chunk<message::compact_filter_range>(empty_raw, wire_version);
    REQUIRE(empty);
    REQUIRE(empty->leaf_count == 0);
    REQUIRE(empty->leaves.empty());
    REQUIRE(to_data_chunk(*empty, wire_version) == empty_raw);
    check_boundaries(*empty);
}

TEST_CASE("cfrange refuses what does not fit its tree", "[script filters][message]") {
    message::compact_filter_range range;
    range.filter_type = message::script_compact_filter;
    range.block_hash = the_block;
    range.leaf_count = 5;
    range.first_index = 1;
    range.leaves = {leaf(0x20, 1, 1, kind::spent), leaf(0x30, 2, 0, kind::output_category)};
    range.before = leaf(0x10, 1, 0, kind::output);
    range.after = leaf(0x40, 3, 2, kind::spent_category);
    range.hashes = hashes(3, 0x01);
    auto const raw = to_data_chunk(range, wire_version);
    REQUIRE(from_data_chunk<message::compact_filter_range>(raw, wire_version));

    auto const unwritten = [](message::compact_filter_range const& message) {
        data_chunk buffer(message.serialized_size(wire_version) + 64);
        byte_writer writer(buffer);
        return ! message.to_data(writer, wire_version);
    };

    // A neighbour missing where the run leaves room for one, or there where it
    // does not.
    auto no_before = range;
    no_before.before.reset();
    REQUIRE(unwritten(no_before));
    auto no_after = range;
    no_after.after.reset();
    REQUIRE(unwritten(no_after));
    auto at_start = range;
    at_start.first_index = 0;
    REQUIRE(unwritten(at_start));
    auto at_end = range;
    at_end.leaf_count = 3;
    REQUIRE(unwritten(at_end));
    // More leaves than the tree has after the first index.
    auto past = range;
    past.leaf_count = 2;
    past.after.reset();
    REQUIRE(unwritten(past));
    // More hashes than two for each level of the deepest tree.
    auto deep = range;
    deep.hashes = hashes(64, 0x01);
    REQUIRE( ! unwritten(deep));
    deep.hashes = hashes(65, 0x01);
    REQUIRE(unwritten(deep));

    // The same, read: the leaf count is at offset 33, the first index at 37
    // and the number of leaves at 41.
    auto short_tree = raw;
    short_tree[33] = 2;
    REQUIRE( ! from_data_chunk<message::compact_filter_range>(short_tree, wire_version));
    // A count of leaves the payload cannot hold is refused before anything is
    // reserved for it.
    auto many = raw;
    many[33] = 0xff;
    many[34] = 0xff;
    many[35] = 0xff;
    many[36] = 0x7f;
    many[41] = 0xfd;
    many.insert(many.begin() + 42, {0xff, 0xff});
    REQUIRE( ! from_data_chunk<message::compact_filter_range>(many, wire_version));
    // A leaf of a kind the tree does not have: the kind is the last byte of
    // the first leaf, at offset 42 + 40. Kind 4, a transaction's leaf, is one
    // it has.
    auto transaction = raw;
    transaction[82] = 4;
    auto const with_transaction = from_data_chunk<message::compact_filter_range>(transaction, wire_version);
    REQUIRE(with_transaction);
    REQUIRE(with_transaction->leaves[0].kind == kind::transaction);
    auto strange = raw;
    strange[82] = 5;
    REQUIRE( ! from_data_chunk<message::compact_filter_range>(strange, wire_version));
    strange[82] = 0xff;
    REQUIRE( ! from_data_chunk<message::compact_filter_range>(strange, wire_version));
}

TEST_CASE("getcfproof is a type, a block and a leaf index", "[script filters][message]") {
    auto const raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf03000000");
    REQUIRE(raw.size() == 37);
    auto const parsed = from_data_chunk<message::get_compact_filter_proof>(raw, wire_version);
    REQUIRE(parsed);
    REQUIRE(*parsed == message::get_compact_filter_proof{message::script_compact_filter, the_block, 3});
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);
}

TEST_CASE("cfproof of a created output is the leaf, its branch and its transaction", "[script filters][message]") {
    auto const raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf040000000200000033343536373839"
        "3a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152010000000100000000024142434445464748494a4b4c4d"
        "4e4f505152535455565758595a5b5c5d5e5f6048494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f6061626364"
        "656667020000000105040706010003020d0c0f0e09080b0a15141716111013121d1c1f1e19181b1a05000000025105ff"
        "ffffff02ed030000000000001976a91405060708090a0b0c0d0e0f10111213141516171888acd5070000000000001976"
        "a914060708090a0b0c0d0e0f1011121314151617181988ac0f000000016162636465666768696a6b6c6d6e6f70717273"
        "7475767778797a7b7c7d7e7f80");
    auto const parsed = from_data_chunk<message::compact_filter_proof>(raw, wire_version);
    REQUIRE(parsed);
    message::compact_filter_proof expected;
    expected.filter_type = message::script_compact_filter;
    expected.block_hash = the_block;
    expected.leaf_count = 4;
    expected.leaf_index = 2;
    expected.leaf = leaf(0x33, 1, 1, kind::output);
    expected.leaf_branch = hashes(2, 0x41);
    expected.transaction = parsed_transaction(
        "020000000105040706010003020d0c0f0e09080b0a15141716111013121d1c1f1e19181b1a05000000025105ffffffff"
        "02ed030000000000001976a91405060708090a0b0c0d0e0f10111213141516171888acd5070000000000001976a91406"
        "0708090a0b0c0d0e0f1011121314151617181988ac0f000000");
    expected.transaction_branch = hashes(1, 0x61);
    REQUIRE(*parsed == expected);
    REQUIRE( ! parsed->origin);
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);
}

TEST_CASE("cfproof of a transaction's leaf is the leaf, its branch and that transaction", "[script filters][message]") {
    // The proof of the created output above with the leaf of its transaction
    // instead: index 0 at offset 41 + 36, kind 4 at 41 + 40.
    auto raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf040000000200000033343536373839"
        "3a3b3c3d3e3f404142434445464748494a4b4c4d4e4f505152010000000000000004024142434445464748494a4b4c4d"
        "4e4f505152535455565758595a5b5c5d5e5f6048494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f6061626364"
        "656667020000000105040706010003020d0c0f0e09080b0a15141716111013121d1c1f1e19181b1a05000000025105ff"
        "ffffff02ed030000000000001976a91405060708090a0b0c0d0e0f10111213141516171888acd5070000000000001976"
        "a914060708090a0b0c0d0e0f1011121314151617181988ac0f000000016162636465666768696a6b6c6d6e6f70717273"
        "7475767778797a7b7c7d7e7f80");
    REQUIRE(raw[77] == 0);
    REQUIRE(raw[81] == 4);
    auto const parsed = from_data_chunk<message::compact_filter_proof>(raw, wire_version);
    REQUIRE(parsed);
    REQUIRE(parsed->leaf == leaf(0x33, 1, 0, kind::transaction));
    REQUIRE(parsed->leaf_branch == hashes(2, 0x41));
    REQUIRE(parsed->transaction == parsed_transaction(
        "020000000105040706010003020d0c0f0e09080b0a15141716111013121d1c1f1e19181b1a05000000025105ffffffff"
        "02ed030000000000001976a91405060708090a0b0c0d0e0f10111213141516171888acd5070000000000001976a91406"
        "0708090a0b0c0d0e0f1011121314151617181988ac0f000000"));
    REQUIRE(parsed->transaction_branch == hashes(1, 0x61));
    REQUIRE( ! parsed->origin);
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);

    // The leaf names the transaction itself, not an output it spends, so a
    // proof with an origin is not written.
    auto with_origin = *parsed;
    with_origin.origin = message::compact_filter_proof_origin{};
    data_chunk buffer(with_origin.serialized_size(wire_version) + 64);
    byte_writer writer(buffer);
    REQUIRE( ! with_origin.to_data(writer, wire_version));

    // A kind above 4 is refused.
    raw[81] = 5;
    REQUIRE( ! from_data_chunk<message::compact_filter_proof>(raw, wire_version));
}

TEST_CASE("cfproof of a spent output adds the transaction that created it", "[script filters][message]") {
    auto const raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf09000000080000004445464748494a"
        "4b4c4d4e4f505152535455565758595a5b5c5d5e5f60616263020000000000000001017172737475767778797a7b7c7d"
        "7e7f808182838485868788898a8b8c8d8e8f90020000000106070405020300010e0f0c0d0a0b08091617141512131011"
        "1e1f1c1d1a1b181906000000025106ffffffff01ee030000000000001976a914060708090a0b0c0d0e0f101112131415"
        "1617181988ac12000000028182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa088898a8b8c"
        "8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4"
        "f5f6f7f8f9fafbfcfdfeff09000000020000000107060504030201000f0e0d0c0b0a090817161514131211101f1e1d1c"
        "1b1a191807000000025107ffffffff03ef030000000000001976a9140708090a0b0c0d0e0f101112131415161718191a"
        "88acd7070000000000001976a91408090a0b0c0d0e0f101112131415161718191a1b88acbf0b0000000000001976a914"
        "090a0b0c0d0e0f101112131415161718191a1b1c88ac15000000049192939495969798999a9b9c9d9e9fa0a1a2a3a4a5"
        "a6a7a8a9aaabacadaeafb098999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b79fa0a1a2a3"
        "a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbea6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9ba"
        "bbbcbdbebfc0c1c2c3c4c5");
    auto const parsed = from_data_chunk<message::compact_filter_proof>(raw, wire_version);
    REQUIRE(parsed);
    message::compact_filter_proof expected;
    expected.filter_type = message::script_compact_filter;
    expected.block_hash = the_block;
    expected.leaf_count = 9;
    expected.leaf_index = 8;
    expected.leaf = leaf(0x44, 2, 0, kind::spent);
    expected.leaf_branch = hashes(1, 0x71);
    expected.transaction = parsed_transaction(
        "020000000106070405020300010e0f0c0d0a0b080916171415121310111e1f1c1d1a1b181906000000025106ffffffff"
        "01ee030000000000001976a914060708090a0b0c0d0e0f1011121314151617181988ac12000000");
    expected.transaction_branch = hashes(2, 0x81);
    message::compact_filter_proof_origin origin;
    for (size_t i = 0; i < hash_size; ++i) origin.block_hash[i] = uint8_t(0xe0 ^ i);
    origin.transaction_index = 9;
    origin.transaction = parsed_transaction(
        "020000000107060504030201000f0e0d0c0b0a090817161514131211101f1e1d1c1b1a191807000000025107ffffffff"
        "03ef030000000000001976a9140708090a0b0c0d0e0f101112131415161718191a88acd7070000000000001976a91408"
        "090a0b0c0d0e0f101112131415161718191a1b88acbf0b0000000000001976a914090a0b0c0d0e0f1011121314151617"
        "18191a1b1c88ac15000000");
    origin.transaction_branch = hashes(4, 0x91);
    expected.origin = origin;
    REQUIRE(*parsed == expected);
    REQUIRE(to_data_chunk(*parsed, wire_version) == raw);
    check_boundaries(*parsed);

    // The leaf of a spent token category carries one too, and both branches
    // can be empty: a tree of one leaf, a block of one transaction.
    auto const category_raw = bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf010000000000000055565758595a5b"
        "5c5d5e5f606162636465666768696a6b6c6d6e6f707172737401000000000000000300020000000108090a0b0c0d0e0f"
        "000102030405060718191a1b1c1d1e1f101112131415161708000000025108ffffffff01f0030000000000001976a914"
        "08090a0b0c0d0e0f101112131415161718191a1b88ac1800000001a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5"
        "b6b7b8b9babbbcbdbebfc0e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff0000000002"
        "0000000109080b0a0d0c0f0e010003020504070619181b1a1d1c1f1e111013121514171609000000025109ffffffff01"
        "f1030000000000001976a914090a0b0c0d0e0f101112131415161718191a1b1c88ac1b00000000");
    auto const category = from_data_chunk<message::compact_filter_proof>(category_raw, wire_version);
    REQUIRE(category);
    REQUIRE(category->leaf == leaf(0x55, 1, 0, kind::spent_category));
    REQUIRE(category->leaf_branch.empty());
    REQUIRE(category->transaction == parsed_transaction(
        "020000000108090a0b0c0d0e0f000102030405060718191a1b1c1d1e1f101112131415161708000000025108ffffffff"
        "01f0030000000000001976a91408090a0b0c0d0e0f101112131415161718191a1b88ac18000000"));
    REQUIRE(category->transaction_branch == hashes(1, 0xa1));
    REQUIRE(category->origin);
    REQUIRE(category->origin->transaction_index == 0);
    REQUIRE(category->origin->transaction == parsed_transaction(
        "020000000109080b0a0d0c0f0e010003020504070619181b1a1d1c1f1e111013121514171609000000025109ffffffff"
        "01f1030000000000001976a914090a0b0c0d0e0f101112131415161718191a1b1c88ac1b000000"));
    REQUIRE(category->origin->transaction_branch.empty());
    REQUIRE(to_data_chunk(*category, wire_version) == category_raw);
    check_boundaries(*category);
}

TEST_CASE("cfproof refuses a leaf outside its tree and an origin that does not match the leaf",
          "[script filters][message]") {
    auto const spent = from_data_chunk<message::compact_filter_proof>(bytes(
        "01b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf09000000080000004445464748494a"
        "4b4c4d4e4f505152535455565758595a5b5c5d5e5f60616263020000000000000001017172737475767778797a7b7c7d"
        "7e7f808182838485868788898a8b8c8d8e8f90020000000106070405020300010e0f0c0d0a0b08091617141512131011"
        "1e1f1c1d1a1b181906000000025106ffffffff01ee030000000000001976a914060708090a0b0c0d0e0f101112131415"
        "1617181988ac12000000028182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa088898a8b8c"
        "8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4"
        "f5f6f7f8f9fafbfcfdfeff09000000020000000107060504030201000f0e0d0c0b0a090817161514131211101f1e1d1c"
        "1b1a191807000000025107ffffffff03ef030000000000001976a9140708090a0b0c0d0e0f101112131415161718191a"
        "88acd7070000000000001976a91408090a0b0c0d0e0f101112131415161718191a1b88acbf0b0000000000001976a914"
        "090a0b0c0d0e0f101112131415161718191a1b1c88ac15000000049192939495969798999a9b9c9d9e9fa0a1a2a3a4a5"
        "a6a7a8a9aaabacadaeafb098999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b79fa0a1a2a3"
        "a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbea6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9ba"
        "bbbcbdbebfc0c1c2c3c4c5"), wire_version);
    REQUIRE(spent);

    auto const unwritten = [](message::compact_filter_proof const& message) {
        data_chunk buffer(message.serialized_size(wire_version) + 64);
        byte_writer writer(buffer);
        return ! message.to_data(writer, wire_version);
    };
    REQUIRE( ! unwritten(*spent));

    // The index of a leaf is below the number of leaves.
    auto outside = *spent;
    outside.leaf_index = outside.leaf_count;
    REQUIRE(unwritten(outside));
    // A spent output's leaf without the transaction that created it.
    auto bare = *spent;
    bare.origin.reset();
    REQUIRE(unwritten(bare));
    // A created output's leaf with one.
    auto extra = *spent;
    extra.leaf.kind = kind::output;
    REQUIRE(unwritten(extra));
    // A branch deeper than a tree of 2^32 leaves.
    auto deep = *spent;
    deep.leaf_branch = hashes(32, 0x01);
    REQUIRE( ! unwritten(deep));
    deep.leaf_branch = hashes(33, 0x01);
    REQUIRE(unwritten(deep));

    // Read: the leaf count is at offset 33 and the leaf index at 37.
    auto raw = to_data_chunk(*spent, wire_version);
    raw[37] = 9;
    REQUIRE( ! from_data_chunk<message::compact_filter_proof>(raw, wire_version));
    // Read as the leaf of a created output, the origin is left over: the kind
    // is the last byte of the leaf, at offset 41 + 40.
    auto relabelled = to_data_chunk(*spent, wire_version);
    relabelled[81] = 0;
    REQUIRE( ! from_data_chunk<message::compact_filter_proof>(relabelled, wire_version));
    // And as the leaf of a transaction.
    relabelled[81] = 4;
    REQUIRE( ! from_data_chunk<message::compact_filter_proof>(relabelled, wire_version));
}
