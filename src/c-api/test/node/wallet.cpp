// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The C API of wallet support, on a chain the node stores: requests and
// answers in Electrum's JSON-RPC, where it stands as JSON, and a handle
// released while it runs.

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/handles.h>
#include <kth/capi/node/wallet.h>

#include "../../../blockchain/test/reorg_chain_fixture.hpp"

namespace {

struct answer_slot {
    std::promise<std::pair<kth_error_code_t, std::string>> done;
};

void on_answer(kth_wallet_mut_t, void* ctx, kth_error_code_t status, char* answer) {
    auto* slot = static_cast<answer_slot*>(ctx);
    std::string text = answer != nullptr ? answer : "";
    kth_core_destruct_string(answer);
    slot->done.set_value({status, text});
}

// One request, its answer within 30 seconds.
std::pair<kth_error_code_t, std::string> ask(kth_wallet_mut_t wallet, char const* request) {
    answer_slot slot;
    auto future = slot.done.get_future();
    kth_node_wallet_async_request(wallet, request, &slot, on_answer);
    REQUIRE(future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    return future.get();
}

void on_run(kth_wallet_mut_t, void* ctx, kth_error_code_t status) {
    static_cast<std::promise<kth_error_code_t>*>(ctx)->set_value(status);
}

// Reads the handle it is given, as a handler may.
struct run_reading {
    std::atomic<bool> read{false};
    std::string status;
};

void on_run_reading(kth_wallet_mut_t self, void* ctx, kth_error_code_t) {
    auto* reading = static_cast<run_reading*>(ctx);
    char* status = kth_node_wallet_status(self);
    reading->status = status != nullptr ? status : "";
    kth_core_destruct_string(status);
    reading->read = true;
}

std::string status_of(kth_wallet_const_t wallet) {
    char* status = kth_node_wallet_status(wallet);
    REQUIRE(status != nullptr);
    std::string const text = status;
    kth_core_destruct_string(status);
    return text;
}

} // namespace

TEST_CASE("C wallet from the node's own chain answers requests and reports where it stands", "[capi][wallet]") {
    kth::test::chain_fixture fixture("capi_wallet_answers");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto* const chain = static_cast<kth_chain_t>(&fixture.chain());

    kth_wallet_mut_t wallet = nullptr;
    REQUIRE(kth_node_wallet_construct_from_chain(chain, kth_network_regtest, 0, &wallet) == kth_ec_success);
    REQUIRE(wallet != nullptr);

    // The chain holds the genesis block alone, and every answer reaches it.
    auto const status = status_of(wallet);
    REQUIRE(status.find("\"source\":\"chain\"") != std::string::npos);
    REQUIRE(status.find("\"synced\":true") != std::string::npos);
    REQUIRE(status.find("\"tip\":0") != std::string::npos);

    auto const [code, version] = ask(wallet, R"({"jsonrpc":"2.0","id":3,"method":"server.version","params":[]})");
    REQUIRE(code == kth_ec_success);
    REQUIRE(version.find("Knuth") != std::string::npos);
    REQUIRE(version.find("\"id\":3") != std::string::npos);
    REQUIRE(ask(wallet, R"({"jsonrpc":"2.0","id":5,"method":"blockchain.headers.get_tip","params":[]})").second.find("\"height\":0") != std::string::npos);

    // Refused requests are answered with their error; a notification with nothing.
    REQUIRE(ask(wallet, R"({"id":4,"method":"no.such","params":[]})").second.find("-32601") != std::string::npos);
    REQUIRE(ask(wallet, "not json").second.find("-32700") != std::string::npos);
    auto const notified = ask(wallet, R"({"jsonrpc":"2.0","method":"server.ping","params":[]})");
    REQUIRE(notified.first == kth_ec_success);
    REQUIRE(notified.second.empty());

    std::promise<kth_error_code_t> ran;
    auto ran_future = ran.get_future();
    kth_node_wallet_async_run(wallet, &ran, on_run);
    kth_node_wallet_stop(wallet);
    REQUIRE(ran_future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);

    kth_node_wallet_destruct(wallet);
    kth_node_wallet_destruct(nullptr);
}

TEST_CASE("C wallet released while it runs: the run handler gets the handle whole, before the release returns", "[capi][wallet]") {
    kth::test::chain_fixture fixture("capi_wallet_release");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    kth_wallet_mut_t wallet = nullptr;
    REQUIRE(kth_node_wallet_construct_from_chain(static_cast<kth_chain_t>(&fixture.chain()), kth_network_regtest, 0,
        &wallet) == kth_ec_success);
    run_reading reading;
    kth_node_wallet_async_run(wallet, &reading, on_run_reading);
    // Released at once, without a stop first.
    kth_node_wallet_destruct(wallet);
    REQUIRE(reading.read);
    REQUIRE(reading.status.find("\"source\":\"chain\"") != std::string::npos);
}
