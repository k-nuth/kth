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

TEST_CASE("render_network_info serializes the getnetworkinfo fields", "[rpc query]") {
    // NETWORK, BITCOIN_CASH and NETWORK_LIMITED, plus bit 30, which has no name.
    auto const services = std::uint64_t((1u << 0) | (1u << 5) | (1u << 10) | (1u << 30));
    auto const json = render_network_info({
        .version = "1.5.2",
        .subversion = "/Knuth:1.5.2(EB32.0)/",
        .protocol_version = 70016u,
        .services = services,
        .relay = true,
        .connections = 8u,
        .byte_fee_satoshis = 0.1f});
    auto const fee = json.find(R"("relayfee":)");
    REQUIRE(fee != std::string::npos);
    REQUIRE(json.substr(0, fee) ==
        R"({"version":1050200,"subversion":"/Knuth:1.5.2(EB32.0)/","protocolversion":70016,)"
        R"("localservices":"0000000040000421",)"
        R"("localservicesnames":["NETWORK","BITCOIN_CASH","NETWORK_LIMITED","UNKNOWN[2^30]"],)"
        R"("localrelay":true,"networkactive":true,"connections":8,)");
    // 0.1 satoshi per byte is 100 satoshis per kB.
    REQUIRE(std::stod(json.substr(fee + 11)) == 0.000001);
    REQUIRE(json.ends_with(R"(,"warnings":""})"));
}

TEST_CASE("render_network_info reads the version number part by part", "[rpc query]") {
    auto const version = [](std::string_view text) {
        auto const json = render_network_info({.version = text, .subversion = "", .protocol_version = 0u,
            .services = 0u, .relay = false, .connections = 0u, .byte_fee_satoshis = 0.0f});
        return json.substr(0, json.find(','));
    };
    REQUIRE(version("1.5.2") == R"({"version":1050200)");
    REQUIRE(version("12.0.7") == R"({"version":12000700)");
    REQUIRE(version("2.1") == R"({"version":2010000)");
    REQUIRE(version("1.5.2-rc1") == R"({"version":1050200)");
    REQUIRE(version("unknown") == R"({"version":0)");
}
