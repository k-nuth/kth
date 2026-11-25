// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The node's local Electrum listener, the compatibility layer for wallets that
// do not speak P2P: Fulcrum's methods, argument orders, limits and result
// shapes, answered from the node's own chain.

#include "wallet_chain_fixture.hpp"

#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>

#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/node/parser.hpp>
#include <kth/node/wallet/electrum.hpp>

namespace {

uint64_t coinbase_value(chain::block const& blk) {
    return blk.transactions().front().outputs()[0].value();
}

std::string raw_of(chain::transaction const& tx) {
    data_chunk bytes(tx.serialized_size(true));
    byte_writer writer(bytes);
    REQUIRE(tx.to_data(writer, true));
    return encode_base16(bytes);
}

// The node's parser on these arguments: whether it accepts them, the
// listener it keeps, and what it says.
std::tuple<bool, std::string, std::string> parse_node(std::vector<std::string> const& extra) {
    node::parser parser(domain::config::network::regtest);
    std::vector<std::string> args{"kth", "--network", "regtest"};
    args.insert(args.end(), extra.begin(), extra.end());
    std::vector<char const*> argv;
    for (auto const& arg : args) argv.push_back(arg.c_str());
    std::ostringstream error;
    auto const ok = parser.parse(int(argv.size()), argv.data(), error);
    return {ok, parser.configured.node.wallet_electrum, error.str()};
}

} // namespace

TEST_CASE("wallet Electrum listener answers in Fulcrum's shapes, from the node's own chain", "[wallet][wallet_electrum]") {
    // Blocks 1 to 110 pay the miner; block 101 spends block 1's coinbase back
    // to it, so that coinbase is fully spent.
    stored_chain stored("wallet_electrum_answers", 110, 101);
    auto own = wallet::service::from_chain(stored.fixture.chain(), domain::config::network::regtest);
    auto& service = *own;
    auto const& spend = *stored.spend;
    auto const first = stored.blocks[0].transactions().front();
    auto const second = stored.blocks[1].transactions().front();
    auto const branch5 = wait_for(service, service.header_branch(5, 10));
    REQUIRE(branch5);

    wallet::electrum_server electrum(service, domain::config::network::regtest);
    auto const ask = [&](std::string const& method, std::string const& params) {
        return wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","id":7,"method":")" + method + R"(","params":)" + params + "}"));
    };
    auto const hash_text = "\"" + encode_hash(sha256_hash(test::miner_script().bytes())) + "\"";

    // Every coinbase but the first, and the spend's output.
    uint64_t unspent_value = spend.outputs()[0].value();
    for (size_t at = 1; at < stored.blocks.size(); ++at) unspent_value += coinbase_value(stored.blocks[at]);
    REQUIRE(ask("blockchain.scripthash.get_balance", "[" + hash_text + "]") ==
        R"({"jsonrpc":"2.0","result":{"confirmed":)" + std::to_string(unspent_value) + R"(,"unconfirmed":0},"id":7})");
    REQUIRE(ask("blockchain.scripthash.get_balance", "[" + hash_text + ",\"bad\"]").find(R"("code":-32602)") != std::string::npos);

    auto const listed = ask("blockchain.scripthash.get_history", "[" + hash_text + "]");
    REQUIRE(listed.find(encode_hash(spend.hash()) + R"(","height":101})") != std::string::npos);
    REQUIRE(listed.find(encode_hash(first.hash()) + R"(","height":1})") != std::string::npos);
    // to_height is exclusive, and from_height may not pass it.
    auto const ranged = ask("blockchain.scripthash.get_history", "[" + hash_text + ",101,102]");
    REQUIRE(ranged.find(encode_hash(spend.hash())) != std::string::npos);
    REQUIRE(ranged.find(encode_hash(stored.blocks[101].transactions().front().hash())) == std::string::npos);
    REQUIRE(ranged.find(encode_hash(stored.blocks[99].transactions().front().hash())) == std::string::npos);
    REQUIRE(ask("blockchain.scripthash.get_history", "[" + hash_text + ",12,11]").find(R"("code":-32602)") != std::string::npos);

    // In chain order: block 2's coinbase first, and nothing spent.
    auto const unspent = ask("blockchain.scripthash.listunspent", "[" + hash_text + "]");
    REQUIRE(unspent.rfind(R"({"jsonrpc":"2.0","result":[{"tx_hash":")" + encode_hash(second.hash()) +
        R"(","tx_pos":0,"height":2,"value":)" + std::to_string(coinbase_value(stored.blocks[1])) + "}", 0) == 0);
    REQUIRE(unspent.find(encode_hash(first.hash())) == std::string::npos);

    // The fully spent coinbase, by its txid alone, and where it is; a txid
    // no block holds is refused.
    REQUIRE(ask("blockchain.transaction.get", "[\"" + encode_hash(first.hash()) + "\"]") ==
        R"({"jsonrpc":"2.0","result":")" + raw_of(first) + R"(","id":7})");
    REQUIRE(ask("blockchain.transaction.get_height", "[\"" + encode_hash(spend.hash()) + "\"]") ==
        R"({"jsonrpc":"2.0","result":101,"id":7})");
    REQUIRE(ask("blockchain.transaction.get_merkle", "[\"" + encode_hash(first.hash()) + "\"]").find(R"("block_height":1,)") != std::string::npos);
    REQUIRE(ask("blockchain.transaction.get", "[\"" + encode_hash(sha256_hash(data_chunk{0x42})) + "\"]").find(R"("error")") != std::string::npos);

    // The tip the answers reach: the node's connected tip.
    REQUIRE(ask("blockchain.headers.get_tip", "[]").find(R"("height":110)") != std::string::npos);

    // RPA: at most 60 blocks from from_height, to_height exclusive, and a
    // prefix shorter than 8 bits refused, as Fulcrum answers.
    char prefix_text[8];
    std::snprintf(prefix_text, sizeof(prefix_text), "%04x", unsigned(chain::input_prefix(spend.inputs()[0])));
    auto const rpa = [&](std::string const& range) {
        return ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text + "\"," + range + "]");
    };
    REQUIRE(rpa("60").find(encode_hash(spend.hash())) != std::string::npos);
    REQUIRE(rpa("0").find(encode_hash(spend.hash())) == std::string::npos);
    REQUIRE(rpa("60,101").find(encode_hash(spend.hash())) == std::string::npos);
    REQUIRE(rpa("101,102").find(encode_hash(spend.hash())) != std::string::npos);
    REQUIRE(ask("blockchain.rpa.get_history", std::string("[\"") + prefix_text[0] + "\",0]").find(R"("code":-32602)") != std::string::npos);

    // Requests that are not answered with a result.
    REQUIRE(ask("no.such.method", "[]").find(R"("code":-32601)") != std::string::npos);
    REQUIRE(wait_for(service, electrum.answer("not json")).find(R"("code":-32700)") != std::string::npos);
    // A notification (no id) gets no answer; a batch gets one array.
    REQUIRE(wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","method":"server.ping","params":[]})")).empty());
    auto const batch = wait_for(service, electrum.answer(R"([{"id":1,"method":"server.ping","params":[]},{"id":2,"method":"server.version","params":[]}])"));
    REQUIRE(batch.front() == '[');
    REQUIRE(batch.find(R"("result":null,"id":1)") != std::string::npos);

    // An output by its outpoint: unspent, spent by a later block, and one
    // that does not exist.
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(second.hash()) + "\",0]") ==
        R"({"jsonrpc":"2.0","result":{"confirmed_height":2,"scripthash":)" + hash_text + R"(,"value":)" +
        std::to_string(coinbase_value(stored.blocks[1])) + R"(},"id":7})");
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(first.hash()) + "\",0]") ==
        R"({"jsonrpc":"2.0","result":null,"id":7})");
    REQUIRE(ask("blockchain.utxo.get_info", "[\"" + encode_hash(second.hash()) + "\",9]") ==
        R"({"jsonrpc":"2.0","result":null,"id":7})");

    // Header proofs, in Fulcrum's shapes; none to a checkpoint past the tip.
    REQUIRE(ask("blockchain.block.header", "[5,10]").find(R"("root":")" + encode_hash(branch5->second) + "\"") != std::string::npos);
    REQUIRE(ask("blockchain.block.headers", "[3,4,10]").find(R"("root":")" + encode_hash(branch5->second) + "\"") != std::string::npos);
    REQUIRE(ask("blockchain.block.header", "[5,200]").find(R"("code":-32602)") != std::string::npos);

    // No double-spend proof held.
    REQUIRE(ask("blockchain.transaction.dsproof.list", "[]") == R"({"jsonrpc":"2.0","result":[],"id":7})");
    REQUIRE(ask("blockchain.transaction.dsproof.get", "[\"" + encode_hash(spend.hash()) + "\"]") == R"({"jsonrpc":"2.0","result":null,"id":7})");
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
    // [::1] loses its brackets, as the node binds it.
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
    auto const loopback = parse_node({"--node.wallet_electrum", "127.0.0.1:50001"});
    REQUIRE(std::get<0>(loopback));
    REQUIRE(std::get<1>(loopback) == "127.0.0.1:50001");
    REQUIRE(std::get<0>(parse_node({"--node.wallet_electrum", "[::1]:50001"})));
    REQUIRE( ! std::get<0>(parse_node({"--node.wallet_electrum", "0.0.0.0:50001"})));
    REQUIRE( ! std::get<0>(parse_node({"--node.wallet_electrum", "192.168.0.160:50001"})));

    // And on a pruning node, which removes the blocks the wallet reads back
    // to its birthday; a pruning node without the listener is not refused.
    auto const pruned = parse_node({"--db.prune_keep_blocks", "1000", "--node.wallet_electrum", "127.0.0.1:50001"});
    REQUIRE( ! std::get<0>(pruned));
    REQUIRE(std::get<2>(pruned).find("pruning") != std::string::npos);
    REQUIRE(std::get<0>(parse_node({"--db.prune_keep_blocks", "1000"})));

    // The node's wallet reads from a birthday it is given.
    node::parser parser(domain::config::network::regtest);
    char const* argv[]{"kth", "--network", "regtest", "--node.wallet_birthday", "800000"};
    std::ostringstream error;
    REQUIRE(parser.parse(5, argv, error));
    REQUIRE(parser.configured.node.wallet_birthday == 800000u);
}

TEST_CASE("wallet Electrum listener binds loopback addresses only, and answers over TCP", "[wallet][wallet_electrum]") {
    stored_chain stored("wallet_electrum_listen", 12, std::nullopt);
    auto own = wallet::service::from_chain(stored.fixture.chain(), domain::config::network::regtest);
    auto& service = *own;
    for (auto const* refused : {"0.0.0.0", "192.168.1.10", "10.0.0.1", "::", "2001:db8::1", "localhost", "[::1]"}) {
        CAPTURE(refused);
        wallet::electrum_server listener(service, domain::config::network::regtest);
        auto const bound = listener.listen(refused, 0);
        REQUIRE( ! bound);
        REQUIRE(bound.error() == error::listen_failed);
    }

    // A port the OS assigns, reported by the listener.
    wallet::electrum_server listener(service, domain::config::network::regtest);
    auto const bound = listener.listen("127.0.0.1", 0);
    REQUIRE(bound);
    REQUIRE(*bound != 0);
    auto running = ::asio::co_spawn(service.executor(), listener.run(), ::asio::use_future);

    // A request line in, its answer line out; every wait bounded.
    ::asio::io_context client_io;
    ::asio::ip::tcp::socket socket(client_io);
    std::error_code ec;
    socket.connect(::asio::ip::tcp::endpoint(::asio::ip::make_address("127.0.0.1"), *bound), ec);
    REQUIRE( ! ec);
    std::string const request = R"({"jsonrpc":"2.0","id":3,"method":"blockchain.headers.get_tip","params":[]})" "\n";
    ::asio::write(socket, ::asio::buffer(request), ec);
    REQUIRE( ! ec);
    std::string line;
    std::optional<std::error_code> read;
    ::asio::async_read_until(socket, ::asio::dynamic_buffer(line), '\n', [&](std::error_code done, size_t) {
        read = done;
    });
    client_io.run_for(30s);
    REQUIRE(read);
    REQUIRE( ! *read);
    REQUIRE(line.find(R"("height":12)") != std::string::npos);
    REQUIRE(line.find(R"("id":3)") != std::string::npos);
    socket.close();

    listener.stop();
    REQUIRE(running.wait_for(30s) == std::future_status::ready);
    running.get();
}
