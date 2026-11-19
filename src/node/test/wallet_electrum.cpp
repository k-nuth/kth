// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The node's local Electrum listener, the compatibility layer for wallets that
// do not speak P2P: Fulcrum's methods, argument orders, limits and result
// shapes, answered from wallet support in the same process.

#include "wallet_peer_harness.hpp"

#include <sstream>

#include <kth/node/parser.hpp>
#include <kth/node/wallet/electrum.hpp>

TEST_CASE("wallet Electrum listener answers in Fulcrum's shapes", "[wallet][wallet_electrum]") {
    answers_chain made;
    auto const& paying = made.paying;
    auto const& spend = made.spend;
    auto const& again = made.again;
    auto const& unrelated = made.unrelated;
    auto const& ground = made.ground;
    test_peer peer(made.served);
    temporary_directory dir("electrum");
    running_service running(settings_for(dir.path, {peer.endpoint()}));
    auto& service = *running.service;
    REQUIRE(wait_for(service, service.wait_synced(30s)));
    auto const value = chain::input_prefix(ground.inputs()[0]);
    auto const branch5 = wait_for(service, service.header_branch(5, 10));
    REQUIRE(branch5);

    // The local Electrum listener answers the same, in Fulcrum's shapes.
    wallet::electrum_server electrum(service, domain::config::network::regtest);
    auto const ask = [&](std::string const& method, std::string const& params) {
        return wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","id":7,"method":")" + method + R"(","params":)" + params + "}"));
    };
    auto const hash_text = "\"" + encode_hash(sha256_hash(watched_script)) + "\"";
    REQUIRE(ask("blockchain.scripthash.get_balance", "[" + hash_text + "]") ==
        R"({"jsonrpc":"2.0","result":{"confirmed":500,"unconfirmed":0},"id":7})");
    auto const listed = ask("blockchain.scripthash.get_history", "[" + hash_text + "]");
    REQUIRE(listed.find(encode_hash(paying.hash()) + R"(","height":5})") != std::string::npos);
    REQUIRE(listed.find(encode_hash(again.hash()) + R"(","height":12})") != std::string::npos);
    auto const ranged = ask("blockchain.scripthash.get_history", "[" + hash_text + ",6,12]");
    REQUIRE(ranged.find(encode_hash(spend.hash())) != std::string::npos);
    REQUIRE(ranged.find(encode_hash(again.hash())) == std::string::npos);
    REQUIRE(ask("blockchain.scripthash.listunspent", "[" + hash_text + "]") ==
        R"({"jsonrpc":"2.0","result":[{"tx_hash":")" + encode_hash(again.hash()) + R"(","tx_pos":0,"height":12,"value":500}],"id":7})");
    REQUIRE(ask("blockchain.scripthash.get_balance", "[" + hash_text + ",\"bad\"]").find(R"("code":-32602)") != std::string::npos);
    auto const raw = ask("blockchain.transaction.get", "[\"" + encode_hash(unrelated.hash()) + "\"]");
    data_chunk unrelated_bytes(unrelated.serialized_size(true));
    byte_writer unrelated_writer(unrelated_bytes);
    REQUIRE(unrelated.to_data(unrelated_writer, true));
    REQUIRE(raw.find("\"" + encode_base16(unrelated_bytes) + "\"") != std::string::npos);
    REQUIRE(ask("blockchain.headers.get_tip", "[]").find(R"("height":30)") != std::string::npos);
    REQUIRE(ask("blockchain.transaction.get_height", "[\"" + encode_hash(unrelated.hash()) + "\"]") ==
        R"({"jsonrpc":"2.0","result":15,"id":7})");
    char prefix_text[8];
    std::snprintf(prefix_text, sizeof(prefix_text), "%04x", unsigned(value));
    REQUIRE(ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text + "\",0]").find(encode_hash(ground.hash())) != std::string::npos);
    // to_height is exclusive, and a prefix shorter than 8 bits is refused, as Fulcrum refuses it.
    REQUIRE(ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text + "\",0,20]").find(encode_hash(ground.hash())) == std::string::npos);
    REQUIRE(ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text + "\",20,21]").find(encode_hash(ground.hash())) != std::string::npos);
    REQUIRE(ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text[0] + "\",0]").find(R"("code":-32602)") != std::string::npos);
    REQUIRE(ask("no.such.method", "[]").find(R"("code":-32601)") != std::string::npos);
    REQUIRE(wait_for(service, electrum.answer("not json")).find(R"("code":-32700)") != std::string::npos);
    auto const batch = wait_for(service, electrum.answer(R"([{"id":1,"method":"server.ping","params":[]},{"id":2,"method":"server.version","params":[]}])"));
    REQUIRE(batch.front() == '[');
    REQUIRE(batch.find(R"("result":null,"id":1)") != std::string::npos);

    // An output by its outpoint: unspent, and spent by a later block.
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(again.hash()) + "\",0]") ==
        R"({"jsonrpc":"2.0","result":{"confirmed_height":12,"scripthash":)" + hash_text + R"(,"value":500},"id":7})");
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(paying.hash()) + "\",0]") ==
        R"({"jsonrpc":"2.0","result":null,"id":7})");
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(paying.hash()) + "\",9]") ==
        R"({"jsonrpc":"2.0","result":null,"id":7})");

    // Header proofs, in Fulcrum's shapes.
    auto const proven = ask("blockchain.block.header", "[5,10]");
    REQUIRE(proven.find(R"("root":")" + encode_hash(branch5->second) + "\"") != std::string::npos);
    REQUIRE(ask("blockchain.block.headers", "[3,4,10]").find(R"("root":")" + encode_hash(branch5->second) + "\"") != std::string::npos);
    REQUIRE(ask("blockchain.block.header", "[5,40]").find(R"("code":-32602)") != std::string::npos);
    REQUIRE(ask("blockchain.transaction.dsproof.list", "[]") == R"({"jsonrpc":"2.0","result":[],"id":7})");
    REQUIRE(ask("blockchain.transaction.dsproof.get", "[\"" + encode_hash(again.hash()) + "\"]") == R"({"jsonrpc":"2.0","result":null,"id":7})");
}

// The node's own wallet listener
// ----------------------------------------------------------------------------

TEST_CASE("wallet Electrum listener shows the tip its answers reach, not the headers'", "[wallet][wallet_electrum]") {
    // Headers from a peer that serves no filters: nothing below them can be
    // answered yet, so the tip shown is genesis.
    served_chain served;
    for (uint32_t height = 1; height <= 10; ++height) served.mine({});
    test_peer headers_only(served, false);
    temporary_directory dir("electrum_tip");
    running_service running(settings_for(dir.path, {headers_only.endpoint()}));
    auto& service = *running.service;
    REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.header_height == 10u; }));
    wallet::electrum_server electrum(service, domain::config::network::regtest);
    auto const tip = wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","id":1,"method":"blockchain.headers.get_tip","params":[]})"));
    REQUIRE(tip.find(R"("height":0,)") != std::string::npos);
}

TEST_CASE("wallet listener of the node accepts loopback addresses only", "[wallet]") {
    for (auto const* accepted : {"127.0.0.1:50001", "127.5.6.7:0", "[::1]:50001"}) {
        CAPTURE(accepted);
        REQUIRE( ! node::wallet::electrum_bind_refusal(accepted));
    }
    for (auto const* refused : {"0.0.0.0:50001", "192.168.1.10:50001", "10.0.0.1:1", "[::]:50001",
                                "[2001:db8::1]:50001", "localhost:50001", "127.0.0.1", "127.0.0.1:", "127.0.0.1:65536",
                                "127.0.0.1:port"}) {
        CAPTURE(refused);
        REQUIRE(node::wallet::electrum_bind_refusal(refused));
    }
    REQUIRE(node::wallet::electrum_bind_refusal("0.0.0.0:50001")->find("loopback") != std::string::npos);
    // [::1] loses its brackets, as kth-wallet and the node bind it.
    auto const v6 = node::wallet::parse_electrum_bind("[::1]:50001");
    REQUIRE(v6);
    REQUIRE(v6->address == "::1");
    REQUIRE(v6->port == 50001);
    auto const v4 = node::wallet::parse_electrum_bind("127.0.0.1:0");
    REQUIRE(v4);
    REQUIRE(v4->address == "127.0.0.1");
    REQUIRE(v4->port == 0);
    REQUIRE( ! node::wallet::parse_electrum_bind("[0.0.0.0]:50001"));

    // The option refuses the same at startup.
    auto const parse = [](std::string const& value) {
        node::parser parser(domain::config::network::regtest);
        std::vector<std::string> args{"kth", "--network", "regtest", "--node.wallet_electrum", value};
        std::vector<char const*> argv;
        for (auto const& arg : args) argv.push_back(arg.c_str());
        std::ostringstream error;
        auto const ok = parser.parse(int(argv.size()), argv.data(), error);
        return std::pair{ok, parser.configured.node.wallet_electrum};
    };
    REQUIRE(parse("127.0.0.1:50001") == std::pair{true, std::string("127.0.0.1:50001")});
    REQUIRE(parse("[::1]:50001").first);
    REQUIRE( ! parse("0.0.0.0:50001").first);
    REQUIRE( ! parse("192.168.0.160:50001").first);

    // The node's wallet reads from a birthday it is given.
    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--node.wallet_birthday", "800000"};
    std::ostringstream error;
    REQUIRE(parser.parse(5, argv, error));
    REQUIRE(parser.configured.node.wallet_birthday == 800000u);
}

TEST_CASE("wallet Electrum listener binds loopback addresses only", "[wallet][wallet_electrum]") {
    // A service that is never run: only its executor is used.
    temporary_directory dir("listen");
    auto made = wallet::service::from_peers(settings_for(dir.path, {"127.0.0.1:1"}));
    REQUIRE(made);
    auto& service = **made;
    for (auto const* refused : {"0.0.0.0", "192.168.1.10", "10.0.0.1", "::", "2001:db8::1", "localhost", "[::1]"}) {
        CAPTURE(refused);
        wallet::electrum_server listener(service, domain::config::network::regtest);
        auto const bound = listener.listen(refused, 0);
        REQUIRE( ! bound);
        REQUIRE(bound.error() == error::listen_failed);
    }
    wallet::electrum_server listener(service, domain::config::network::regtest);
    auto const bound = listener.listen("127.0.0.1", 0);
    REQUIRE(bound);
    REQUIRE(*bound != 0);
    listener.stop();
}
