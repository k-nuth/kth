// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <kth/capi/chain/chain_mining.h>
#include <kth/blockchain/interface/block_chain.hpp>
#include <iostream>
#include <stdexcept>

namespace {
struct completion {
    unsigned calls = 0;
    kth_chain_t chain = nullptr;
    kth_error_code_t status = kth_ec_success;
    kth_mining_info_t info{};
};
void receive(kth_chain_t chain, void* context, kth_error_code_t status, kth_mining_info_t info) {
    auto& out = *static_cast<completion*>(context);
    ++out.calls;
    out.chain = chain;
    out.status = status;
    out.info = info;
}
void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
void check(kth::blockchain::scenario mode, kth_error_code_t expected) {
    kth::blockchain::block_chain chain{{}, mode};
    completion out;
    kth_chain_async_mining_info(&chain, &out, receive);
    chain.context.run();
    require(out.calls == 1, "terminal callback must be delivered exactly once");
    require(out.chain == &chain, "callback must retain the original chain and context");
    require(out.status == expected, "incorrect terminal status");
    if (expected == kth_ec_success) {
        require(out.info.blocks == 42 && out.info.difficulty == 1.5 && out.info.pooled_tx == 3,
                "successful mining information lost");
    } else {
        require(out.info.blocks == 0 && out.info.difficulty == 0 && out.info.pooled_tx == 0
                && out.info.chain == 0 && !out.info.transition_in_progress && !out.info.caught_up
                && !out.info.fresh, "failure payload must be zero initialized");
    }
    chain.context.restart();
    chain.context.poll();
    require(out.calls == 1, "duplicate terminal callback");
}
}
int main() {
    using kth::blockchain::scenario;
    try {
        check(scenario::success, kth_ec_success);
        check(scenario::domain_error, kth_ec_operation_failed);
        check(scenario::allocation, kth_ec_out_of_memory);
        check(scenario::exception, kth_ec_unhandled_exception);
        check(scenario::startup_allocation, kth_ec_out_of_memory);
        check(scenario::startup_exception, kth_ec_unhandled_exception);
        std::cout << "PASS: success, domain failure, async allocation/exception, startup allocation/exception\n";
    } catch (std::exception const& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
