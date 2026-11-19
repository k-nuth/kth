// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <kth/capi/handles.h>
#include <kth/capi/node/wallet.h>

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

} // namespace

TEST_CASE("C wallet from peers answers requests and reports where it stands", "[capi][wallet]") {
    auto const directory = std::filesystem::temp_directory_path() / "kth_capi_wallet";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    kth_wallet_mut_t refused = nullptr;
    REQUIRE(kth_node_wallet_construct_from_peers(kth_network_regtest, directory.string().c_str(), nullptr, 0, 0,
        nullptr, &refused) != kth_ec_success);
    REQUIRE(refused == nullptr);

    // A peer that is not there: the handle works, with nothing synced.
    char const* peers[] = {"127.0.0.1:1"};
    kth_wallet_mut_t wallet = nullptr;
    REQUIRE(kth_node_wallet_construct_from_peers(kth_network_regtest, directory.string().c_str(), peers, 1, 0,
        nullptr, &wallet) == kth_ec_success);
    REQUIRE(wallet != nullptr);

    char* status = kth_node_wallet_status(wallet);
    REQUIRE(status != nullptr);
    std::string const text = status;
    kth_core_destruct_string(status);
    REQUIRE(text.find("\"source\":\"peers\"") != std::string::npos);
    REQUIRE(text.find("\"synced\":false") != std::string::npos);

    answer_slot version;
    kth_node_wallet_async_request(wallet, R"({"jsonrpc":"2.0","id":3,"method":"server.version","params":[]})",
        &version, on_answer);
    auto future = version.done.get_future();
    REQUIRE(future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    auto const [code, answer] = future.get();
    REQUIRE(code == kth_ec_success);
    REQUIRE(answer.find("Knuth") != std::string::npos);
    REQUIRE(answer.find("\"id\":3") != std::string::npos);

    answer_slot unknown;
    kth_node_wallet_async_request(wallet, R"({"id":4,"method":"no.such","params":[]})", &unknown, on_answer);
    auto unknown_future = unknown.done.get_future();
    REQUIRE(unknown_future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    REQUIRE(unknown_future.get().second.find("-32601") != std::string::npos);

    std::promise<kth_error_code_t> ran;
    kth_node_wallet_async_run(wallet, &ran, on_run);
    kth_node_wallet_stop(wallet);
    auto ran_future = ran.get_future();
    REQUIRE(ran_future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);

    kth_node_wallet_destruct(wallet);
    kth_node_wallet_destruct(nullptr);
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("C wallet released while it runs: the run handler gets the handle whole, before the release returns", "[capi][wallet]") {
    auto const directory = std::filesystem::temp_directory_path() / "kth_capi_wallet_release";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    char const* peers[] = {"127.0.0.1:1"};
    kth_wallet_mut_t wallet = nullptr;
    REQUIRE(kth_node_wallet_construct_from_peers(kth_network_regtest, directory.string().c_str(), peers, 1, 0,
        nullptr, &wallet) == kth_ec_success);
    run_reading reading;
    kth_node_wallet_async_run(wallet, &reading, on_run_reading);
    // Released at once, without a stop first.
    kth_node_wallet_destruct(wallet);
    REQUIRE(reading.read);
    REQUIRE(reading.status.find("\"source\":\"peers\"") != std::string::npos);
    std::filesystem::remove_all(directory, ignored);
}
