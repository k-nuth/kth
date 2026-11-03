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
// One hash, 4423cfd2...0fc3, under each address form used below.
constexpr auto pkh_script = "76a9144423cfd20e8f5f10863ae31df5634620d36b0fc388ac";
constexpr auto sh_script = "a9144423cfd20e8f5f10863ae31df5634620d36b0fc387";

std::string valid(std::string_view address, std::string_view script, bool is_script, bool token_aware) {
    return std::string(R"({"isvalid":true,"address":")") + std::string(address) +
        R"(","scriptPubKey":")" + std::string(script) + R"(","isscript":)" +
        (is_script ? "true" : "false") + R"(,"istokenaware":)" + (token_aware ? "true" : "false") + "}";
}
} // namespace

TEST_CASE("render_validate_address describes P2PKH and P2SH addresses", "[rpc query]") {
    using domain::config::network;
    REQUIRE(render_validate_address("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", network::mainnet) ==
        valid("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", pkh_script, false, false));
    REQUIRE(render_validate_address("bitcoincash:ppzz8n7jp6847yyx8t33matrgcsdx6c0cvvwyry2nl", network::mainnet) ==
        valid("bitcoincash:ppzz8n7jp6847yyx8t33matrgcsdx6c0cvvwyry2nl", sh_script, true, false));
    REQUIRE(render_validate_address("bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc", network::regtest) ==
        valid("bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc", pkh_script, false, false));
    REQUIRE(render_validate_address("bchtest:ppzz8n7jp6847yyx8t33matrgcsdx6c0cvguqyxa5r", network::chipnet) ==
        valid("bchtest:ppzz8n7jp6847yyx8t33matrgcsdx6c0cvguqyxa5r", sh_script, true, false));
}

TEST_CASE("render_validate_address keeps the token-aware signal", "[rpc query]") {
    using domain::config::network;
    REQUIRE(render_validate_address("bitcoincash:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvup2jd0h3", network::mainnet) ==
        valid("bitcoincash:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvup2jd0h3", pkh_script, false, true));
    REQUIRE(render_validate_address("bchtest:rpzz8n7jp6847yyx8t33matrgcsdx6c0cv0kn6gmts", network::testnet4) ==
        valid("bchtest:rpzz8n7jp6847yyx8t33matrgcsdx6c0cv0kn6gmts", sh_script, true, true));
}

TEST_CASE("render_validate_address returns legacy and prefix-less input as cashaddr", "[rpc query]") {
    using domain::config::network;
    REQUIRE(render_validate_address("17DHrHvtmMRs9ciersFCPNhvJtryd5NWbT", network::mainnet) ==
        valid("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", pkh_script, false, false));
    REQUIRE(render_validate_address("mmjF9M1saNs7vjCGaSDaDHvFAtTgUNtfrJ", network::regtest) ==
        valid("bchreg:qpzz8n7jp6847yyx8t33matrgcsdx6c0cv99t2zdvc", pkh_script, false, false));
    REQUIRE(render_validate_address("qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", network::mainnet) ==
        valid("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", pkh_script, false, false));
    REQUIRE(render_validate_address("BITCOINCASH:QPZZ8N7JP6847YYX8T33MATRGCSDX6C0CVMTEVRFGZ", network::mainnet) ==
        valid("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", pkh_script, false, false));
}

TEST_CASE("render_validate_address pays a 32-byte script hash with P2SH32", "[rpc query]") {
    using domain::config::network;
    // Hash 000102...1f.
    REQUIRE(render_validate_address(
                "bitcoincash:pvqqzqsrqszsvpcgpy9qkrqdpc83qygjzv2p29shrqv35xcur50p7h2c7ctj5", network::mainnet) ==
        valid("bitcoincash:pvqqzqsrqszsvpcgpy9qkrqdpc83qygjzv2p29shrqv35xcur50p7h2c7ctj5",
              "aa20000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f87", true, false));
}

TEST_CASE("render_validate_address rejects other networks and non-addresses", "[rpc query]") {
    using domain::config::network;
    constexpr auto invalid = R"({"isvalid":false})";
    // Another network's cashaddr prefix, and another network's legacy version.
    REQUIRE(render_validate_address("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz", network::regtest) == invalid);
    REQUIRE(render_validate_address("bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707", network::regtest) == invalid);
    REQUIRE(render_validate_address("17DHrHvtmMRs9ciersFCPNhvJtryd5NWbT", network::chipnet) == invalid);
    REQUIRE(render_validate_address("mmjF9M1saNs7vjCGaSDaDHvFAtTgUNtfrJ", network::mainnet) == invalid);
    // A 32-byte public key hash, which no standard output pays to.
    REQUIRE(render_validate_address(
                "bitcoincash:qvqqzqsrqszsvpcgpy9qkrqdpc83qygjzv2p29shrqv35xcur50p77hk2ql29", network::mainnet) == invalid);
    // A broken checksum, and no address at all.
    REQUIRE(render_validate_address("bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfga", network::mainnet) == invalid);
    REQUIRE(render_validate_address("not an address", network::mainnet) == invalid);
}
