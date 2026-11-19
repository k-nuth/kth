// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers, as real WASM in a Worker, against the
// compact filter peer in fixtures.py. The control mines a short regtest chain
// and builds each block's script and input prefix filters with the node's own
// builders, hands them to the fixture, and then asks wallet support, through a
// WebSocket bridge and on the Worker's runtime, for a history, a balance, a
// transaction by its hash alone and an RPA prefix. The scans run inline, and
// nothing sent names what was watched or looked up.

#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/network/browser_stream.hpp>
#include <kth/node/wallet/service.hpp>
#include <asio/co_spawn.hpp>
#include <asio/read.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/blockchain/test/regtest_miner.hpp"

kth::browser::runtime owner;

namespace {

using namespace kth;
using namespace std::chrono_literals;
namespace chain = kth::domain::chain;
namespace wallet = kth::node::wallet;
constexpr auto regtest = domain::config::network::regtest;
constexpr uint32_t length = 30;
constexpr char sent_log[] = "/kth-wallet/sent.log";

void require(bool condition, std::string const& what) {
    if ( ! condition) throw std::runtime_error(what);
}

data_chunk decoded(std::string_view hex) {
    auto bytes = decode_base16(hex);
    return bytes ? std::move(*bytes) : data_chunk{};
}

data_chunk const watched_script = decoded("76a914111111111111111111111111111111111111111188ac");
data_chunk const other_script = decoded("76a914222222222222222222222222222222222222222288ac");

hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0xa5};
    return sha256_hash(bytes);
}

// Wallet support checks blocks against their headers and filters, not the
// scripts they run: inputs may spend outputs made up for the control.
chain::transaction spending(std::vector<chain::point> const& previous, data_chunk const& script, uint64_t value, uint32_t salt) {
    chain::input::list inputs;
    for (auto const& point : previous) {
        inputs.emplace_back(chain::output_point{point.hash(), point.index()},
            chain::script(data_chunk{0x01, uint8_t(salt), uint8_t(salt >> 8)}, false), 0xfffffffe);
    }
    chain::output::list outputs;
    outputs.emplace_back(value, chain::script(script, false), chain::token_data_opt{});
    return chain::transaction(2, 0, std::move(inputs), std::move(outputs));
}

void put(data_chunk& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(uint8_t(value >> shift));
}

void put(data_chunk& out, byte_span bytes) {
    put(out, uint32_t(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// What the fixture serves: "KTHW", the number of heights, then each block
// with its script filter and its input prefix filter, from genesis.
data_chunk served(std::vector<chain::block> const& blocks) {
    std::map<chain::point, chain::output> created;
    data_chunk out{'K', 'T', 'H', 'W'};
    put(out, uint32_t(blocks.size()));
    for (auto const& blk : blocks) {
        // The outputs its inputs spend, as the node's undo record holds them;
        // one this chain did not make is taken for one of another script.
        std::vector<chain::output> spent;
        auto const& txs = blk.transactions();
        for (size_t at = 1; at < txs.size(); ++at) {
            for (auto const& in : txs[at].inputs()) {
                auto const found = created.find(chain::point(in.previous_output().hash(), in.previous_output().index()));
                spent.push_back(found != created.end() ? found->second
                    : chain::output(1, chain::script(other_script, false), chain::token_data_opt{}));
            }
        }
        for (auto const& tx : txs) {
            for (uint32_t vout = 0; vout < tx.outputs().size(); ++vout) {
                created.insert_or_assign(chain::point(tx.hash(), vout), tx.outputs()[vout]);
            }
        }
        data_chunk raw(blk.serialized_size());
        byte_writer writer(raw);
        require(bool(blk.to_data(writer)), "a block did not serialize");
        auto const script = chain::build_script_filter(blk, spent);
        require(bool(script), "a script filter was not built");
        put(out, raw);
        put(out, chain::to_data(*script));
        put(out, chain::to_data(chain::build_input_prefix_filter(blk)));
    }
    return out;
}

::asio::awaitable<void> upload(data_chunk const& data) {
    auto executor = co_await ::asio::this_coro::executor;
    network::browser_stream stream(executor);
    co_await stream.async_connect("ws://127.0.0.1:18815", ::asio::use_awaitable);
    co_await ::asio::async_write(stream, ::asio::buffer(data), ::asio::use_awaitable);
    std::array<uint8_t, 2> answer{};
    co_await ::asio::async_read(stream, ::asio::buffer(answer), ::asio::use_awaitable);
    require(answer[0] == 'o' && answer[1] == 'k', "the fixture did not take the chain");
    std::error_code ignored;
    stream.close(ignored);
}

struct mined {
    std::vector<chain::block> blocks{chain::block::genesis_regtest()};
    chain::transaction paying;
    chain::transaction spend;
    chain::transaction again;
    chain::transaction unrelated;
    chain::transaction ground;
};

// Height 5 pays the watched script, 9 spends that coin, 12 pays it again,
// 15 has a transaction no target touches, 20 one whose first input a sender
// ground for RPA.
mined mine() {
    mined out;
    out.paying = spending({chain::point(digest_of(100), 0)}, watched_script, 1000, 1);
    out.spend = spending({chain::point(out.paying.hash(), 0)}, other_script, 900, 2);
    out.again = spending({chain::point(digest_of(101), 1)}, watched_script, 500, 3);
    out.unrelated = spending({chain::point(digest_of(102), 0)}, other_script, 77, 4);
    out.ground = spending({chain::point(digest_of(103), 0), chain::point(digest_of(104), 0)}, other_script, 66, 5);
    auto const base_time = uint32_t(std::time(nullptr)) - 100 * 600;
    for (uint32_t height = 1; height <= length; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 5) txs.push_back(out.paying);
        if (height == 9) txs.push_back(out.spend);
        if (height == 12) txs.push_back(out.again);
        if (height == 15) txs.push_back(out.unrelated);
        if (height == 20) txs.push_back(out.ground);
        out.blocks.push_back(test::mine_block(out.blocks.back().hash(), height, base_time + height * 600, height, std::move(txs), 0));
    }
    return out;
}

::asio::awaitable<void> ask(wallet::service& service, mined const& chain_made) {
    auto const synced = co_await service.wait_synced(30s);
    require(bool(synced), "wallet support did not sync: " + service.current().error);
    require(synced->header_height == length && synced->tip == length, "wallet support synced to another height");
    std::printf("synced headers=%u script_filters=%u peers=%zu\n", synced->header_height, *synced->tip, synced->peers);

    auto const target = wallet::target::of_script(watched_script);
    auto const history = co_await service.history(target);
    require(bool(history), "no history");
    require(history->confirmed.size() == 3 && history->confirmed[0].txid == chain_made.paying.hash() &&
        history->confirmed[0].height == 5 && history->confirmed[1].txid == chain_made.spend.hash() &&
        history->confirmed[2].txid == chain_made.again.hash(), "the history is not the chain's");
    auto const balance = co_await service.get_balance(target, wallet::token_filter::include);
    require(balance && balance->confirmed == 500 && balance->unconfirmed == 0, "the balance is not the chain's");
    auto const coins = co_await service.unspent(target, wallet::token_filter::include);
    require(coins && coins->size() == 1 && coins->front().outpoint == chain::point(chain_made.again.hash(), 0),
        "the unspent outputs are not the chain's");
    std::printf("history entries=%zu balance=%llu unspent=%zu\n", history->confirmed.size(),
        (unsigned long long)balance->confirmed, coins->size());

    // A transaction no watched target touches, by its hash alone.
    auto const found = co_await service.transaction(chain_made.unrelated.hash(), std::nullopt);
    require(found && found->confirmed && found->confirmed->height == 15, "a transaction was not found by its hash");
    auto const missing = co_await service.transaction(digest_of(999), std::nullopt);
    require( ! missing, "a transaction that does not exist was found");

    // RPA: every transaction with a counted input in the prefix.
    auto const prefix = chain::input_prefix(chain_made.ground.inputs()[0]);
    auto const matches = co_await service.rpa_history(prefix, 16, 0, std::nullopt);
    require(matches && std::ranges::any_of(*matches, [&](auto const& match) {
        return match.txid == chain_made.ground.hash() && match.height == 20u; }), "the RPA prefix did not find its transaction");
    std::printf("transaction by hash height=%u rpa matches=%zu\n", found->confirmed->height, matches->size());

    // Nothing sent names what was watched or looked up.
    std::vector<std::string> const secrets{
        encode_base16(watched_script),
        encode_base16(chain::script_key(watched_script)),
        encode_base16(chain::transaction_key(chain_made.unrelated.hash())),
        encode_base16(chain_made.unrelated.hash()),
    };
    std::ifstream log(sent_log);
    std::string line;
    size_t lines = 0;
    while (std::getline(log, line)) {
        ++lines;
        auto const command = line.substr(0, line.find(' '));
        require(command != "getcfrange" && command != "getcfproof" && command != "filterload" && command != "filteradd",
            "a request that names keys was sent: " + command);
        for (auto const& secret : secrets) require(line.find(secret) == std::string::npos, "a sent message names a secret");
    }
    require(lines > 0, "nothing was logged as sent");
    std::printf("sent messages=%zu none names a watched key\n", lines);
}

::asio::awaitable<void> control() {
    auto executor = co_await ::asio::this_coro::executor;
    auto const chain_made = mine();
    co_await upload(served(chain_made.blocks));
    std::printf("chain served heights=%zu\n", chain_made.blocks.size());

    wallet::peer_settings settings;
    settings.network = regtest;
    settings.directory = "/kth-wallet";
    settings.peers = {"ws://127.0.0.1:18816"};
    settings.request_timeout = 20s;
    settings.header_capacity = 1000;
    settings.sent_log = sent_log;
    auto made = wallet::service::from_peers(settings);
    require(bool(made), "wallet support was refused");
    auto service = std::move(*made);

    // The service runs beside the questions and is stopped and joined
    // whatever they find, before it is destroyed.
    std::exception_ptr failure;
    {
        kth::task_group running("wallet control", executor);
        running.spawn("wallet support", [&service]() -> ::asio::awaitable<void> { (void)co_await service->run(); });
        try {
            co_await ask(*service, chain_made);
        } catch (...) {
            failure = std::current_exception();
        }
        service->stop();
        co_await running.join();
    }
    service.reset();
    if (failure) std::rethrow_exception(failure);
}

} // namespace

int main() {
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) {
            try { std::rethrow_exception(error); }
            catch (std::exception const& e) { std::printf("WALLET FAILED: %s\n", e.what()); }
            catch (...) { std::puts("WALLET FAILED"); }
        } else {
            std::puts("wallet control joined");
        }
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n", owner.timers(), owner.streams()); });
    });
    owner.wake();
}
