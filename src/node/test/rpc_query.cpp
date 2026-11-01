// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/node/rpc/query.hpp>

using namespace kth;
using namespace kth::node::rpc;

namespace {
// encode_hash(null_hash) — 64 zero hex chars.
constexpr auto zeros = "0000000000000000000000000000000000000000000000000000000000000000";
} // namespace

// Start Test Suite: rpc query tests

TEST_CASE("render_blockchain_info serializes the getblockchaininfo fields", "[rpc query]") {
    REQUIRE(render_blockchain_info(
                "Mainnet", /*blocks*/ 10u, /*headers*/ 12u,
                "00ff", /*difficulty*/ 1.0) ==
        R"({"chain":"Mainnet","blocks":10,"headers":12,"bestblockhash":"00ff","difficulty":1.0})");
}

TEST_CASE("transaction_to_hex round-trips through the wire format", "[rpc query]") {
    domain::chain::transaction tx{1u, 7u, {}, {}};

    auto const hex = transaction_to_hex(tx);
    REQUIRE(hex.size() == tx.serialized_size(true) * 2u);

    auto const bytes = decode_base16(hex);
    REQUIRE(bytes.has_value());
    byte_reader reader(*bytes);
    auto const decoded = domain::chain::transaction::from_data(reader);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->hash() == tx.hash());
}

TEST_CASE("render_hash_list serializes a JSON array of display hashes", "[rpc query]") {
    REQUIRE(render_hash_list({}) == "[]");
    REQUIRE(render_hash_list({null_hash, null_hash}) ==
        std::string("[\"") + zeros + "\",\"" + zeros + "\"]");
}

TEST_CASE("render_mempool_info serializes the getmempoolinfo fields", "[rpc query]") {
    blockchain::mempool_totals totals{/*size*/ 5u, /*bytes*/ 100u, /*total_fee*/ 200u};
    REQUIRE(render_mempool_info(totals) ==
        R"({"size":5,"bytes":100,"total_fee":200})");
}

TEST_CASE("render_mempool_entry serializes fields plus depends/spentby", "[rpc query]") {
    blockchain::mempool_entry_info entry{/*fee*/ 1000u, /*size*/ 250u, /*time*/ 42u};
    REQUIRE(render_mempool_entry(entry, {null_hash}, {}) ==
        std::string(R"({"fee":1000,"size":250,"time":42,"depends":[")") + zeros +
        R"("],"spentby":[]})");
}

TEST_CASE("header_to_hex serializes the 80-byte wire header", "[rpc query]") {
    domain::chain::header h{/*version*/ 2u, null_hash, null_hash,
                           /*timestamp*/ 1000u, /*bits*/ 0x1d00ffffu, /*nonce*/ 42u};

    auto const hex = header_to_hex(h);
    REQUIRE(hex == std::string("02000000") + zeros + zeros + "e8030000" + "ffff001d" + "2a000000");

    auto const bytes = decode_base16(hex);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == 80u);
    byte_reader reader(*bytes);
    auto const decoded = domain::chain::header::from_data(reader);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == h);
}

TEST_CASE("render_block_header serializes the verbose header fields", "[rpc query]") {
    domain::chain::header h{/*version*/ 2u, null_hash, null_hash,
                           /*timestamp*/ 1000u, /*bits*/ 0x1d00ffffu, /*nonce*/ 42u};
    REQUIRE(render_block_header(h, /*height*/ 5u, null_hash) ==
        std::string(R"({"hash":")") + zeros + R"(","height":5,"version":2,)" +
        R"("previousblockhash":")" + zeros + R"(","merkleroot":")" + zeros + R"(",)" +
        R"("time":1000,"bits":"1d00ffff","nonce":42})");
}

TEST_CASE("SHV RPC results preserve wire headers and display hash order", "[rpc query][shv]") {
    auto const header = domain::chain::block::genesis_regtest().header();
    auto const root = domain::chain::hash(header);
    hash_digest sibling{};
    sibling.front() = 0x12;
    sibling.back() = 0xab;
    auto const branch = render_hash_list({sibling});
    auto const base = std::string(R"({"branch":)") + branch + R"(,"header":")"
        + header_to_hex(header);
    REQUIRE(render_shv(header, {sibling}, root, true) ==
        base + R"(","root":")" + encode_hash(root) + R"("})");
    REQUIRE(render_shv(header, {sibling}, root, false) ==
        base + R"(","peak":")" + encode_hash(root) + R"("})");
    REQUIRE(branch == std::string("[\"ab") + std::string(60, '0') + "12\"]");

    auto const suffix = std::string(R"(,"shvroot":")") + encode_hash(root) + R"("})";
    REQUIRE(render_blockchain_info("regtest", 0, 2, encode_hash(root), 1.0, root) ==
        std::string(R"({"chain":"regtest","blocks":0,"headers":2,"bestblockhash":")")
            + encode_hash(root) + R"(","difficulty":1.0)" + suffix);
    auto expected_header = render_block_header(header, 0, root);
    expected_header.pop_back();
    REQUIRE(render_block_header(header, 0, root, root) == expected_header + suffix);
}
