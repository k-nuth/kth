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

namespace {

domain::chain::transaction parse_tx(std::string_view hex) {
    auto const bytes = decode_base16(hex);
    REQUIRE(bytes.has_value());
    byte_reader reader(*bytes);
    auto tx = domain::chain::transaction::from_data(reader, /*wire*/ true);
    REQUIRE(tx.has_value());
    return std::move(*tx);
}

bool contains(std::string const& text, std::string_view fragment) {
    return text.find(fragment) != std::string::npos;
}

constexpr auto regtest = domain::config::network::regtest;
constexpr auto flags = domain::machine::script_flags::all_rules;

} // namespace

TEST_CASE("render_transaction decodes a coinbase", "[rpc query]") {
    auto const tx = parse_tx(
        "02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0b016f0101062f"
        "41424c412fffffffff01dbf2052a010000001976a9146cafb31b9a10a61a992bf94af8db42bdb3a23b1488ac00000000");
    auto const json = render_transaction(tx, regtest, flags);
    CHECK(json.starts_with(
        R"({"txid":"ea1ef3a3b08cd52b724a45be4f2ad0efbf2506f18421fc924c7cdd1fa2a94e5d",)"
        R"("hash":"ea1ef3a3b08cd52b724a45be4f2ad0efbf2506f18421fc924c7cdd1fa2a94e5d",)"
        R"("version":2,"size":96,"locktime":0,)"
        R"("vin":[{"coinbase":"016f0101062f41424c412f","sequence":4294967295}],)"
        R"("vout":[{"value":50.00000219,"n":0,"scriptPubKey":{"asm":")"));
    CHECK(contains(json,
        R"("hex":"76a9146cafb31b9a10a61a992bf94af8db42bdb3a23b1488ac","reqSigs":1,"type":"pubkeyhash",)"
        R"("addresses":["bchreg:qpk2lvcmngg2vx5e90u547xmg27m8g3mzsj5p6rqer"]}}]})"));
}

TEST_CASE("render_transaction_verbose says whether the block it was read from is active", "[rpc query]") {
    auto const tx = parse_tx(
        "02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0b016f0101062f"
        "41424c412fffffffff01dbf2052a010000001976a9146cafb31b9a10a61a992bf94af8db42bdb3a23b1488ac00000000");
    auto const block_hash = domain::chain::block::genesis_regtest().hash();
    auto const quoted = R"("blockhash":")" + encode_hash(block_hash) + '"';

    auto const active = render_transaction_verbose(tx, regtest, flags,
        transaction_location{block_hash, 3, 1296688602});
    CHECK(contains(active, quoted +
        R"(,"confirmations":3,"time":1296688602,"blocktime":1296688602,"in_active_chain":true})"));

    // Off the connected chain there is nothing to count and no time to give.
    auto const stale = render_transaction_verbose(tx, regtest, flags,
        transaction_location{block_hash, -1, 1296688602});
    CHECK(contains(stale, quoted + R"(,"confirmations":0,"in_active_chain":false})"));
}

TEST_CASE("render_transaction decodes a spend with its inputs and outputs", "[rpc query]") {
    auto const tx = parse_tx(
        "02000000013cd49214320f36fd78fd8ed5daf9c25c95d392fbfd9c5e3d1b2da4fa6f7927f0000000006441a51a5935eb9d2b"
        "2c36aa9d1d1d855ee54fac0a1e8e3ef507a2f52eb4afa28997722a551b5449824e7a9cd30a4630881969ad5cb0612a5a8a16"
        "d8db0451581a2f4121028cffeaef2d58730ff8f558425a0fd37d37db7b7bf0679c4990581c4ea84389a6feffffff0280d1f0"
        "08000000001976a914ff5e8c6fe7e2dd7918232f05a4db1fe1239bb07d88aca51f1521010000001976a9147050b925878deb"
        "85c4713fc84b0a165dcde4bf8288ac6e000000");
    auto const json = render_transaction(tx, regtest, flags);
    CHECK(json.starts_with(
        R"({"txid":"227d72a23b337fbccc1a923fe8c0ed6ca7b17df50c44e680cc5a3c21e59949f0",)"
        R"("hash":"227d72a23b337fbccc1a923fe8c0ed6ca7b17df50c44e680cc5a3c21e59949f0",)"
        R"("version":2,"size":219,"locktime":110,)"
        R"("vin":[{"txid":"f027796ffaa42d1b3d5e9cfdfb92d3955cc2f9dad58efd78fd360f321492d43c","vout":0,)"
        R"("scriptSig":{"asm":")"));
    CHECK(contains(json, R"("hex":"41a51a5935eb9d2b2c36aa9d1d1d855ee54fac0a1e8e3ef507a2f52eb4afa28997722a551b)"
        R"(5449824e7a9cd30a4630881969ad5cb0612a5a8a16d8db0451581a2f4121028cffeaef2d58730ff8f558425a0fd37d37db7b)"
        R"(7bf0679c4990581c4ea84389a6"},"sequence":4294967294}],)"));
    CHECK(contains(json, R"("value":1.50000000,"n":0,)"));
    CHECK(contains(json, R"("addresses":["bchreg:qrl4arr0ul3d67gcyvhstfxmrlsj8xas05y033hq96"])"));
    CHECK(contains(json, R"("value":48.49999781,"n":1,)"));
    CHECK(contains(json, R"("addresses":["bchreg:qpc9pwf9s7x7hpwywylusjc2zewume9lsguuvvdnkl"])"));
}

TEST_CASE("render_transaction decodes token data and an OP_RETURN output", "[rpc query]") {
    // A mutable NFT with commitment aabb and 1000 fungible tokens of category
    // 0102...20 (as stored), then OP_RETURN "test".
    auto const tx = parse_tx(
        "020000000122222222222222222222222222222222222222222222222222222222222222220000000000ffffffff02e80300"
        "000000000041ef0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f207102aabbfde80376a91411"
        "1111111111111111111111111111111111111188ac0000000000000000066a047465737400000000");
    auto const json = render_transaction(tx, regtest, flags);
    CHECK(json.starts_with(
        R"({"txid":"a2a8793850e0e4d606e57e73524d958d60b414c4fe93719f50d509b013a77d61",)"));
    CHECK(contains(json, R"("size":140,)"));
    // The scriptPubKey hex leaves the token prefix out.
    CHECK(contains(json,
        R"("hex":"76a914111111111111111111111111111111111111111188ac","reqSigs":1,"type":"pubkeyhash",)"
        R"("addresses":["bchreg:qqg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zy8lyg7mcr"]},)"
        R"("tokenData":{"category":"201f1e1d1c1b1a191817161514131211100f0e0d0c0b0a090807060504030201",)"
        R"("amount":"1000","nft":{"capability":"mutable","commitment":"aabb"}}})"));
    // Unspendable: no reqSigs and no addresses.
    CHECK(contains(json, R"("value":0.00000000,"n":1,)"));
    CHECK(contains(json, R"("hex":"6a0474657374","type":"nulldata"}})"));
}

TEST_CASE("render_transaction lists each key of a bare multisig output", "[rpc query]") {
    auto const tx = parse_tx(
        "020000000133333333333333333333333333333333333333333333333333333333333333330100000000ffffffff01c40900"
        "0000000000475121028cffeaef2d58730ff8f558425a0fd37d37db7b7bf0679c4990581c4ea84389a62103dcfd9e580de35d"
        "8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac86452ae00000000");
    auto const json = render_transaction(tx, regtest, flags);
    CHECK(contains(json, R"("reqSigs":1,"type":"multisig","addresses":[)"
        R"("bchreg:qpk2lvcmngg2vx5e90u547xmg27m8g3mzsj5p6rqer","bchreg:qr06s0d0ruqjds4r6aspsymzu3rhzzwh3gvt9sq7as"])"));
    // The same transaction on mainnet: the network's own prefix.
    CHECK(contains(render_transaction(tx, domain::config::network::mainnet, flags), R"("bitcoincash:)"));
}
