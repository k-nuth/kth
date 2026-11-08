// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/formats/bitcoin.hpp>
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
    REQUIRE(render_blockchain_info({
                .chain = "Mainnet",
                .blocks = 12u,
                .headers = 12u,
                .best_block_hash = null_hash,
                .difficulty = 1.0,
                .median_time = 1000u,
                .chain_work = uint256_t{0x1234},
                .size_on_disk = 4096u,
                .initial_block_download = false}) ==
        std::string(R"({"chain":"Mainnet","blocks":12,"headers":12,"bestblockhash":")") + zeros +
        R"(","difficulty":1.0,"mediantime":1000,"verificationprogress":1.0,)"
        R"("initialblockdownload":false,)"
        R"("chainwork":"0000000000000000000000000000000000000000000000000000000000001234",)"
        R"("size_on_disk":4096,"warnings":""})");
}

TEST_CASE("render_blockchain_info reports a node still syncing", "[rpc query]") {
    // A quarter of the header chain connected, and a tip the header index
    // cannot name yet: no chainwork rather than a made-up one.
    REQUIRE(render_blockchain_info({
                .chain = "Regtest",
                .blocks = 25u,
                .headers = 100u,
                .best_block_hash = null_hash,
                .difficulty = 2.5,
                .median_time = 7u,
                .chain_work = std::nullopt,
                .size_on_disk = 0u,
                .initial_block_download = true}) ==
        std::string(R"({"chain":"Regtest","blocks":25,"headers":100,"bestblockhash":")") + zeros +
        R"(","difficulty":2.5,"mediantime":7,"verificationprogress":0.25,)"
        R"("initialblockdownload":true,"size_on_disk":0,"warnings":""})");
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
    REQUIRE(render_block_header(h, /*height*/ 5u, null_hash, /*confirmations*/ 3) ==
        std::string(R"({"hash":")") + zeros + R"(","confirmations":3,"height":5,"version":2,)" +
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

TEST_CASE("render_block_header reports a block off the connected chain", "[rpc query]") {
    domain::chain::header h{/*version*/ 2u, null_hash, null_hash,
                           /*timestamp*/ 1000u, /*bits*/ 0x1d00ffffu, /*nonce*/ 42u};
    REQUIRE(render_block_header(h, /*height*/ 5u, null_hash, /*confirmations*/ -1).find(
        R"("confirmations":-1,"height":5,)") != std::string::npos);
}

TEST_CASE("render_estimate_fee converts the per-byte fee to BCH per kB", "[rpc query]") {
    // The default 0.1 satoshi per byte is 100 satoshis per kB.
    REQUIRE(render_estimate_fee(0.1f) == render_estimate_fee(0.1000001f));
    REQUIRE(std::stod(render_estimate_fee(0.1f)) == 0.000001);
    REQUIRE(std::stod(render_estimate_fee(1.0f)) == 0.00001);
    REQUIRE(std::stod(render_estimate_fee(2.5f)) == 0.000025);
    REQUIRE(std::stod(render_estimate_fee(0.0f)) == 0.0);
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
    CHECK(contains(json, R"("value":1.5,"n":0,)"));
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
    CHECK(contains(json, R"("value":0,"n":1,)"));
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

TEST_CASE("decimal amount formatting preserves exact units", "[rpc query]") {
    CHECK(satoshi_to_bitcoin(0) == "0");
    CHECK(satoshi_to_bitcoin(1) == "0.00000001");
    CHECK(satoshi_to_bitcoin(10) == "0.0000001");
    CHECK(satoshi_to_bitcoin(100000000) == "1");
    CHECK(satoshi_to_bitcoin(150000000) == "1.5");
    CHECK(satoshi_to_bitcoin(5000000219) == "50.00000219");
    CHECK(satoshi_to_bitcoin(2100000000000000) == "21000000");
    CHECK(satoshi_to_bitcoin(max_uint64) == "184467440737.09551615");
    CHECK(encode_base10(max_uint64, 0) == "18446744073709551615");
    CHECK(encode_base10(1000, 0) == "1000");
    CHECK(encode_base10(101, 2) == "1.01");
    CHECK(encode_base10(1, 255) == "0." + std::string(254, '0') + "1");
}

TEST_CASE("render_network_hash_ps divides the work by the time spanned", "[rpc query]") {
    REQUIRE(std::stod(render_network_hash_ps(uint256_t{100}, uint256_t{1300}, 1000u, 1600u)) == 2.0);
    // No time spanned, or a window given backwards: 0.
    REQUIRE(std::stod(render_network_hash_ps(uint256_t{100}, uint256_t{1300}, 1000u, 1000u)) == 0.0);
    REQUIRE(std::stod(render_network_hash_ps(uint256_t{1300}, uint256_t{100}, 1000u, 1600u)) == 0.0);
}

TEST_CASE("chain_name gives each network's short chain name", "[rpc query]") {
    using domain::config::network;
    static_assert(domain::config::chain_name(network::mainnet) == "main");
    static_assert(domain::config::chain_name(network::regtest) == "regtest");
    REQUIRE(domain::config::chain_name(network::mainnet) == "main");
    REQUIRE(domain::config::chain_name(network::testnet) == "test");
    REQUIRE(domain::config::chain_name(network::testnet4) == "test4");
    REQUIRE(domain::config::chain_name(network::scalenet) == "scale");
    REQUIRE(domain::config::chain_name(network::chipnet) == "chip");
    REQUIRE(domain::config::chain_name(network::tempnet) == "temp");
    REQUIRE(domain::config::chain_name(network::regtest) == "regtest");
}

TEST_CASE("chain_name display variant preserves log names", "[rpc query]") {
    using domain::config::network;
    using domain::config::chain_name;
    using domain::config::chain_name_format;
    static_assert(chain_name(network::mainnet) == "main");
    static_assert(chain_name(network::mainnet, chain_name_format::display) == "Mainnet");
    REQUIRE(chain_name(network::mainnet, chain_name_format::display) == "Mainnet");
    REQUIRE(chain_name(network::testnet, chain_name_format::display) == "Testnet");
    REQUIRE(chain_name(network::regtest, chain_name_format::display) == "Regtest");
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(chain_name(network::testnet4, chain_name_format::display) == "Testnet4");
    REQUIRE(chain_name(network::scalenet, chain_name_format::display) == "Scalenet");
    REQUIRE(chain_name(network::chipnet, chain_name_format::display) == "Chipnet");
    REQUIRE(chain_name(network::tempnet, chain_name_format::display) == "Tempnet");
#endif
}

TEST_CASE("render_chain_tips serializes the getchaintips entries", "[rpc query]") {
    REQUIRE(render_chain_tips({}) == "[]");
    REQUIRE(render_chain_tips({{7u, null_hash, 2u, blockchain::header_index::tip_status::headers_only, 4200}, {5u, null_hash, 0u, blockchain::header_index::tip_status::active, 3000}}) ==
        std::string(R"([{"height":7,)"
#if defined(KTH_CURRENCY_BCH)
        R"("ticks":4200,)"
#endif
        R"("hash":")") + zeros + R"(","branchlen":2,"status":"headers-only"},)" +
        R"({"height":5,)"
#if defined(KTH_CURRENCY_BCH)
        R"("ticks":3000,)"
#endif
        R"("hash":")" + zeros + R"(","branchlen":0,"status":"active"}])");
}

TEST_CASE("render_tx_out serializes the gettxout fields", "[rpc query]") {
    // The token output of the transaction decoded above.
    auto const tx = parse_tx(
        "020000000122222222222222222222222222222222222222222222222222222222222222220000000000ffffffff02e80300"
        "000000000041ef0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f207102aabbfde80376a91411"
        "1111111111111111111111111111111111111188ac0000000000000000066a047465737400000000");
    auto const json = render_tx_out(null_hash, 3, tx.outputs().front(), /*coinbase*/ false, regtest, flags);
    CHECK(json.starts_with(std::string(R"({"bestblock":")") + zeros +
        R"(","confirmations":3,"value":0.00001,"scriptPubKey":{"asm":")"));
    CHECK(json.ends_with(
        R"("hex":"76a914111111111111111111111111111111111111111188ac","reqSigs":1,"type":"pubkeyhash",)"
        R"("addresses":["bchreg:qqg3zyg3zyg3zyg3zyg3zyg3zyg3zyg3zy8lyg7mcr"]},)"
        R"("tokenData":{"category":"201f1e1d1c1b1a191817161514131211100f0e0d0c0b0a090807060504030201",)"
        R"("amount":"1000","nft":{"capability":"mutable","commitment":"aabb"}},"coinbase":false})"));
}

TEST_CASE("render_network_info serializes the getnetworkinfo fields", "[rpc query]") {
    // NETWORK, BITCOIN_CASH and NETWORK_LIMITED, plus bit 30, which has no name.
    auto const services = uint64_t((1u << 0) | (1u << 5) | (1u << 10) | (1u << 30));
    auto const json = render_network_info({
        .version = "1.5.2",
        .subversion = "/Knuth:1.5.2(EB32.0)/",
        .protocol_version = 70016u,
        .services = services,
        .relay = true,
        .connections = 8u,
        .active = true,
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

TEST_CASE("render_raw_mempool_verbose keys each entry by its txid", "[rpc query]") {
    REQUIRE(render_raw_mempool_verbose({}) == "{}");
    REQUIRE(render_raw_mempool_verbose({{null_hash, /*fee*/ 219u, /*size*/ 219u, /*time*/ 42u, {}, {null_hash}}}) ==
        std::string(R"({")") + zeros + R"(":{"fees":{"base":0.00000219,"modified":0.00000219},)" +
        R"("size":219,"time":42,"depends":[],"spentby":[")" + zeros + R"("]}})");
}
#if defined(KTH_CURRENCY_BCH)
#if __has_include(<kth/node/rpc/chain_progress.hpp>)
#include <kth/node/rpc/chain_progress.hpp>
#endif
TEST_CASE("blockchain RPC exposes tick height and both boundary intervals", "[rpc query][faster_blocks]") {
    auto const text = render_blockchain_info({
        .chain = "chip", .blocks = 100, .headers = 100,
        .best_block_hash = null_hash, .difficulty = 1.0,
        .median_time = 0, .chain_work = std::nullopt, .size_on_disk = 0,
        .initial_block_download = false, .progress = chain_progress{60000, 600, 60}});
    CHECK(text.find("\"ticks\":60000") != std::string::npos);
    CHECK(text.find("\"target_time\":600") != std::string::npos);
    CHECK(text.find("\"next_target_time\":60") != std::string::npos);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("header RPC distinguishes the boundary block from its successor", "[rpc query][faster_blocks]") {
    domain::chain::header const header{4, null_hash, null_hash, 1000, 0x1d00ffff, 0};
    auto const before = render_block_header(header, 100, null_hash, 1, chain_progress{60000, 600, 60});
    CHECK(before.find("\"ticks\":60000") != std::string::npos);
    CHECK(before.find("\"target_time\":600") != std::string::npos);
    CHECK(before.find("\"next_target_time\":60") != std::string::npos);
    auto const after = render_block_header(header, 101, null_hash, 1, chain_progress{60060, 60, 60});
    CHECK(after.find("\"ticks\":60060") != std::string::npos);
}
#endif

#if defined(KTH_CURRENCY_BCH)
namespace {
template <typename Location>
void tick_depth_for_verbose_location(Location& location, int64_t depth) {
    // Preserve the same serializer observation on the unchanged API, which
    // cannot accept tick depth yet: the expected JSON fields still fail there.
    if constexpr (requires { location.confirmations_ticks = depth; }) {
        location.confirmations_ticks = depth;
    }
}
template <typename Location>
void own_tick_for_verbose_block(Location& location, chain_progress const& progress) {
    if constexpr (requires { location.progress = progress; }) {
        location.progress = progress;
    }
}
}
TEST_CASE("verbose transaction tick depth preserves omission and stale conventions", "[rpc query][rpc verbose ticks]") {
    domain::chain::transaction tx{2u, 0u, {}, {}};
    transaction_location location{null_hash, 3, 1700000000};
    tick_depth_for_verbose_location(location, 720);
    auto const active = render_transaction_verbose(tx, regtest, flags, location);
    CHECK(contains(active, R"("confirmations":3,"confirmations_ticks":720,)"));
    location.confirmations = -1;
    tick_depth_for_verbose_location(location, -1);
    auto const stale = render_transaction_verbose(tx, regtest, flags, location);
    CHECK(contains(stale, R"("confirmations":0,"confirmations_ticks":0,"in_active_chain":false})"));
    CHECK_FALSE(contains(stale, R"("blocktime":)"));
    auto const mempool = render_transaction_verbose(tx, regtest, flags, std::nullopt);
    CHECK_FALSE(contains(mempool, R"("confirmations":)"));
    CHECK_FALSE(contains(mempool, R"("confirmations_ticks":)"));
}
TEST_CASE("verbose stale block retains its own ticks and negative depth sentinel", "[rpc query][rpc verbose ticks]") {
    auto const block = domain::chain::block::genesis_regtest();
    block_location location{12, -1, 1700000000, std::nullopt, std::nullopt};
    tick_depth_for_verbose_location(location, -1);
    own_tick_for_verbose_block(location, chain_progress{7200, 600, 60});
    for (auto const verbosity_two : {false, true}) {
        auto const json = render_block_verbose(block, location, verbosity_two, regtest, flags);
        CHECK(contains(json, R"("confirmations":-1,"confirmations_ticks":-1,)"));
        CHECK(contains(json, R"("height":12,"ticks":7200,"target_time":600,"next_target_time":60,)"));
    }
}
#endif
