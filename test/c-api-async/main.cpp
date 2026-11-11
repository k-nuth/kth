// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/capi/chain/chain_async.h>
#include <kth/capi/chain/chain_other.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include <type_traits>

// Deterministic fault in the real co_spawn allocation, armed by executor().
void* operator new(std::size_t size) {
    if (control::fail_next_allocation) {
        control::fail_next_allocation = false;
        control::injected_allocation = true;
        throw std::bad_alloc{};
    }
    if (auto* pointer = std::malloc(size ? size : 1))
        return pointer;
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept {
    std::free(pointer);
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete[](void* pointer) noexcept {
    ::operator delete(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

namespace {
using chain_type = kth::blockchain::block_chain;
using fault = control::fault;
int passed = 0, failed = 0;
void require(bool value, char const* message) {
    if (! value)
        throw std::runtime_error(message);
}
struct completion {
    chain_type* chain;
    int calls = 0, errors = 0, stop_after = 0;
    kth_error_code_t status = kth_ec_success;
    bool bad = false, had_message = false;
    std::array<void (*)(void const*), 2> release{};
    unsigned released = 0;
    uint64_t first_scalar = 0, last_scalar = 0;
    bool destroy_on_stop = false;
    int* destroyed = nullptr;
    ~completion() {
        if (destroyed)
            ++*destroyed;
    }
};
template <class T>
void release_as(void const* pointer) {
    auto* value = static_cast<T const*>(pointer);
    if constexpr (requires { value->value; }) {
        if (value->value != 42)
            control::bad_output = true;
    } else if constexpr (requires { value->begin(); })
        for (auto const& item : *value)
            if (item.value != 42)
                control::bad_output = true;
    delete value;
}
template <class T>
void observe(completion& out, T value) {
    if constexpr (std::is_pointer_v<T>) {
        if constexpr (std::is_same_v<T, char const*>) {
            out.had_message = value != nullptr;
        } else if (value) {
            if (out.status != kth_ec_success)
                out.bad = true;
            auto release = out.release[out.released++];
            if (! release)
                out.bad = true;
            else
                release(value);
        }
    } else if constexpr (std::is_same_v<T, kth_hash_t>) {
        if (out.status != kth_ec_success)
            for (auto byte : value.hash)
                if (byte)
                    out.bad = true;
    } else if constexpr (std::is_same_v<T, kth_mining_template_t>) {
        if (out.status != kth_ec_success && (value.version || value.height || value.coinbase_value || value.bits))
            out.bad = true;
        if (out.status == kth_ec_success && (value.height != 42 || value.version != 4))
            out.bad = true;
    } else {
        if (out.status != kth_ec_success && value != 0)
            out.bad = true;
        if (out.status == kth_ec_success) {
            if (! out.first_scalar)
                out.first_scalar = value;
            out.last_scalar = value;
        }
    }
}
template <class... Args>
void receive(kth_chain_t chain, void* ctx, kth_error_code_t ec, Args... values) {
    auto& out = *static_cast<completion*>(ctx);
    ++out.calls;
    out.status = ec;
    out.released = 0;
    if (chain != out.chain)
        out.bad = true;
    (observe(out, values), ...);
}
template <class... Args>
int event(kth_node_t, kth_chain_t chain, void* ctx, kth_error_code_t ec, Args... values) {
    auto& out = *static_cast<completion*>(ctx);
    if (ec != kth_ec_success) {
        ++out.errors;
        if (out.chain->active != 0)
            out.bad = true;  // cleanup must precede the terminal callback
    }
    receive(chain, ctx, ec, values...);
    auto keep = ec == kth_ec_success && (! out.stop_after || out.calls < out.stop_after);
    if (! keep && out.destroy_on_stop)
        delete &out;
    return keep;
}
struct query {
    char const* name;
    std::function<void(chain_type&, completion&)> start;
    bool has_copy = true;
};
kth_hash_t query_hash() {
    kth_hash_t value{};
    value.hash[0] = 7;
    return value;
}
std::vector<query> queries() {
    return {
        { "last_height", [](auto& bc, auto& out) { kth_chain_async_last_height(&bc, &out, receive); }, false },
        { "mining_template", [](auto& bc, auto& out) { out.release[0] = release_as<std::vector<kth::domain::chain::transaction>>; kth_chain_async_mining_template(&bc, 0, &out, receive); } },
        { "header_height", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::chain::header>; kth_chain_async_block_header_by_height(&bc, &out, 42, receive); } },
        { "header_hash", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::chain::header>; kth_chain_async_block_header_by_hash(&bc, &out, query_hash(), receive); } },
        { "block_height", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::block>; kth_chain_async_block_by_height(&bc, &out, 42, receive); } },
        { "block_hash", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::block>; kth_chain_async_block_by_hash(&bc, &out, query_hash(), receive); } },
        { "header_hashes", [](auto& bc, auto& out) { out.release = {release_as<kth::domain::chain::header>, release_as<kth::hashes>}; kth_chain_async_block_header_by_hash_txs_size(&bc, &out, query_hash(), receive); } },
        { "merkle_height", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::merkle_block>; kth_chain_async_merkle_block_by_height(&bc, &out, 42, receive); } },
        { "merkle_hash", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::merkle_block>; kth_chain_async_merkle_block_by_hash(&bc, &out, query_hash(), receive); } },
        { "compact_height", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::compact_block>; kth_chain_async_compact_block_by_height(&bc, &out, 42, receive); } },
        { "compact_hash", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::compact_block>; kth_chain_async_compact_block_by_hash(&bc, &out, query_hash(), receive); } },
        { "block_timestamp", [](auto& bc, auto& out) { kth_chain_async_block_by_height_timestamp(&bc, &out, 42, receive); }, false },
        { "transaction", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::transaction>; kth_chain_async_transaction(&bc, &out, query_hash(), 1, receive); } },
        { "transaction_position", [](auto& bc, auto& out) { kth_chain_async_transaction_position(&bc, &out, query_hash(), 1, receive); }, false },
        { "submit_block", [](auto& bc, auto& out) { kth::domain::chain::block input; kth_chain_async_submit_block(&bc, &out, &input, receive); input.value = 99; } },
        { "organize_transaction", [](auto& bc, auto& out) { kth::domain::chain::transaction input; kth_chain_async_organize_transaction(&bc, &out, &input, receive); input.value = 99; } },
        { "validate_transaction", [](auto& bc, auto& out) { kth::domain::chain::transaction input; kth_chain_transaction_validate(&bc, &out, &input, receive); input.value = 99; } }
    };
}
void check_query(query const& q, fault mode, int copy = 0) {
    chain_type bc;
    completion out{ &bc };
    auto before = control::live;
    control::mode = mode;
    control::copies = 0;
    control::fail_copy = copy;
    control::injected_allocation = false;
    q.start(bc, out);
    if (mode == fault::schedule_allocation)
        require(control::injected_allocation && out.calls == 1, "co_spawn startup fault was not delivered inline");
    bc.drain();
    auto expected = mode == fault::none && copy == 0 ? kth_ec_success : mode == fault::domain                                      ? kth_ec_operation_failed
                                                                    : mode == fault::exception || mode == fault::startup_exception ? kth_ec_unhandled_exception
                                                                                                                                   : kth_ec_out_of_memory;
    require(out.calls == 1, "missing or duplicate terminal callback");
    require(out.status == expected && ! out.bad, "wrong status or nonzero failure payload");
    if (std::string(q.name) == "validate_transaction" && mode == fault::domain)
        require(out.had_message, "domain error message lost");
    if (mode == fault::none && copy == 0) {
        auto name = std::string(q.name);
        if (name == "last_height" || name.find("height") != std::string::npos || name == "header_hash" || name == "block_hash" || name == "merkle_hash" || name == "compact_hash" || name == "header_hashes")
            require(out.first_scalar == 42, "height lost");
        if (name == "transaction" || name == "transaction_position")
            require(out.first_scalar == 2 && out.last_scalar == 42, "transaction location lost");
        if (name == "block_timestamp")
            require(out.first_scalar == 100 && out.last_scalar == 42, "timestamp lost");
    }
    require(control::live == before, "owned output/input leaked after completion");
}
struct subscription {
    char const* name;
    std::function<void(chain_type&, completion&)> start;
    std::function<void(chain_type&)> publish;
    std::function<void(chain_type&)> close;
    std::function<void(chain_type&)> cancel;
};
std::vector<subscription> subscriptions() {
    return {
        { "blocks", [](auto& bc, auto& out) { out.release = {release_as<std::vector<kth::domain::chain::block>>,release_as<std::vector<kth::domain::chain::block>>}; kth_chain_subscribe_blockchain(nullptr, &bc, &out, event); },
          [](auto& bc) { auto incoming=std::make_shared<std::vector<kth::domain::message::block::const_ptr>>(1,std::make_shared<kth::domain::message::block>());auto replaced=std::make_shared<std::vector<kth::domain::message::block::const_ptr>>(1,std::make_shared<kth::domain::message::block>()); require(bc.block_events->wire.try_send(std::error_code{},42,incoming,replaced),"event refused"); },
          [](auto& bc) { bc.block_events->close(); }, [](auto& bc) { bc.block_events->cancel(); } },
        { "transactions", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::message::transaction>; kth_chain_subscribe_transaction(nullptr, &bc, &out, event); },
          [](auto& bc) { require(bc.tx_events->wire.try_send(std::error_code{}, std::make_shared<kth::domain::message::transaction>()), "event refused"); },
          [](auto& bc) { bc.tx_events->close(); }, [](auto& bc) { bc.tx_events->cancel(); } },
        { "proofs", [](auto& bc, auto& out) { out.release[0] = release_as<kth::domain::chain::double_spend_proof>; kth_chain_subscribe_ds_proof(nullptr, &bc, &out, event); },
          [](auto& bc) { require(bc.proof_events->wire.try_send(std::error_code{}, std::make_shared<kth::domain::chain::double_spend_proof>()), "event refused"); },
          [](auto& bc) { bc.proof_events->close(); }, [](auto& bc) { bc.proof_events->cancel(); } }
    };
}
void check_subscription(subscription const& s, int scenario) {
    chain_type bc;
    completion out{ &bc };
    control::mode = fault::none;
    control::copies = 0;
    control::fail_copy = 0;
    if (scenario == 1)
        control::mode = fault::subscribe_allocation;
    if (scenario == 2)
        control::mode = fault::startup_allocation;
    if (scenario == 3)
        control::mode = fault::startup_exception;
    if (scenario == 4)
        control::mode = fault::receive_allocation;
    if (scenario == 5)
        control::mode = fault::receive_exception;
    if (scenario == 6)
        bc.stopped = true;
    if (scenario == 13)
        control::mode = fault::schedule_allocation;
    control::injected_allocation = false;
    s.start(bc, out);
    auto cleanup = [&] { control::mode=fault::none;control::fail_copy=0;bc.cleanup(); };
    try {
        if (scenario == 0) {  // two successful events, explicit caller unsubscribe; no terminal callback
            out.stop_after = 2;
            s.publish(bc);
            bc.poll();
            s.publish(bc);
            bc.drain();
            require(out.calls == 2 && out.errors == 0 && ! out.bad, "event stream or unsubscribe callback count wrong");
        } else if (scenario == 7 || scenario == 8 || scenario == 9) {
            if (scenario == 9)
                s.close(bc);  // stop before the receiver ever starts
            else {
                bc.poll();
                if (scenario == 7)
                    s.close(bc);
                else
                    s.cancel(bc);
            }
            bc.drain();
            require(out.calls == 1 && out.errors == 1 && out.status == kth_ec_service_stopped && ! out.bad, "shutdown/cancellation did not settle cleanly");
        } else if (scenario == 10 || scenario == 11) {
            control::fail_copy = scenario == 10 ? 1 : 2;
            s.publish(bc);
            bc.drain();
            require(out.calls == 1 && out.errors == 1 && out.status == kth_ec_out_of_memory && ! out.bad, "conversion failure not delivered after cleanup");
        } else if (scenario == 14) {
            s.publish(bc);
            bc.poll();
            require(out.calls == 1 && out.errors == 0, "first event lost");
            control::mode = fault::receive_exception;
            s.publish(bc);
            bc.drain();
            require(out.calls == 3 && out.errors == 1 && out.status == kth_ec_unhandled_exception && ! out.bad, "stream failure lost terminal error or retained registration");
        } else if (scenario == 12) {
            s.publish(bc);
            bc.poll();
            require(out.calls == 1 && out.errors == 0, "first event lost");
            s.close(bc);
            bc.drain();
            require(out.calls == 2 && out.errors == 1 && ! out.bad, "event followed by close did not deliver one terminal callback");
        } else {
            bc.drain();
            auto expected = scenario == 3 || scenario == 5 ? kth_ec_unhandled_exception : scenario == 6 ? kth_ec_service_stopped
                                                                                                        : kth_ec_out_of_memory;
            require(out.calls == 1 && out.errors == 1 && out.status == expected && ! out.bad, "failure did not deliver one cleaned-up terminal callback");
        }
        if (scenario == 13)
            require(control::injected_allocation, "subscription scheduling allocation not injected");
        require(bc.active == 0, "subscription left registered");
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
}
template <class F>
void test(std::string const& name, F run) {
    auto before = control::live;
    control::bad_output = false;
    try {
        run();
        require(control::live == before, "payload leaked");
        require(! control::bad_output, "owned result value corrupted");
        ++passed;
    } catch (std::exception const& e) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
    control::mode = fault::none;
    control::fail_copy = 0;
    control::fail_next_allocation = false;
}
}  // namespace
int main() {
    for (auto const& q : queries()) {
        for (auto mode : { fault::none, fault::domain, fault::allocation, fault::exception, fault::startup_allocation, fault::startup_exception, fault::schedule_allocation })
            test(std::string(q.name) + " mode " + std::to_string(int(mode)), [&] { check_query(q, mode); });
        if (q.has_copy)
            test(std::string(q.name) + " copy failure", [&] { check_query(q, fault::none, 1); });
        if (std::string(q.name) == "header_hashes")
            test("second owned result rollback", [&] { check_query(q, fault::none, 2); });
    }
    for (auto const& s : subscriptions())
        for (int scenario = 0; scenario <= 14; ++scenario) {
            if (scenario == 11 && std::string(s.name) != "blocks")
                continue;
            test(std::string(s.name) + " subscription " + std::to_string(scenario), [&] { check_subscription(s, scenario); });
        }
    for (auto const& s : subscriptions())
        test(std::string(s.name) + " context released by unsubscribe", [&] {
            chain_type bc;
            int destroyed = 0;
            auto* out = new completion{ &bc };
            out->stop_after = 1;
            out->destroy_on_stop = true;
            out->destroyed = &destroyed;
            s.start(bc, *out);
            s.publish(bc);
            bc.drain();
            require(destroyed == 1 && bc.active == 0, "context not released safely or subscription retained");
        });
    auto blocks = subscriptions().front();
    test("stale block event skipped", [&] {
        chain_type bc;
        completion out{ &bc };
        out.stop_after = 1;
        blocks.start(bc, out);
        bc.stale = true;
        blocks.publish(bc);
        bc.poll();
        require(out.calls == 0, "stale block delivered");
        bc.stale = false;
        blocks.publish(bc);
        bc.drain();
        require(out.calls == 1 && bc.active == 0 && ! out.bad, "fresh block lost after stale event");
    });
    std::cout << "Passed " << passed << ", failed " << failed << '\n';
    return failed ? 1 : 0;
}
