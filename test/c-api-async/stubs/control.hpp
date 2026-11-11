// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <array>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace control {
enum class fault {
    none,
    domain,
    allocation,
    exception,
    startup_allocation,
    startup_exception,
    subscribe_allocation,
    receive_allocation,
    receive_exception,
    schedule_allocation
};
inline fault mode = fault::none;
inline bool fail_next_allocation = false, injected_allocation = false;
inline bool bad_output = false;
inline int live = 0, copies = 0, fail_copy = 0;
struct payload {
    int value = 42;
    payload() noexcept {
        ++live;
    }
    payload(payload const& other) : value(other.value) {
        if (++copies == fail_copy)
            throw std::bad_alloc{};
        ++live;
    }
    payload(payload&& other) noexcept : value(other.value) {
        ++live;
    }
    payload& operator=(payload const&) = default;
    payload& operator=(payload&&) = default;
    virtual ~payload() {
        --live;
    }
};
inline void throw_query() {
    if (mode == fault::allocation)
        throw std::bad_alloc{};
    if (mode == fault::exception)
        throw std::runtime_error("query failure");
}
}  // namespace control
namespace kth {
using hash_digest = std::array<unsigned char, 32>;
inline hash_digest null_hash{};
namespace error {
inline std::error_code success{};
inline auto service_stopped = std::make_error_code(std::errc::operation_canceled);
}  // namespace error
namespace domain {
namespace chain {
struct header : control::payload {};
struct block : control::payload {};
struct transaction : control::payload {};
struct double_spend_proof : control::payload {};
}  // namespace chain
namespace message {
struct block : chain::block {
    using const_ptr = std::shared_ptr<block const>;
    block() = default;
    explicit block(chain::block const& value) : chain::block(value) {}
};
struct transaction : chain::transaction {
    using const_ptr = std::shared_ptr<transaction const>;
    transaction() = default;
    explicit transaction(chain::transaction const& value) : chain::transaction(value) {}
};
struct merkle_block : control::payload {};
struct compact_block : control::payload {};
}  // namespace message
}  // namespace domain
struct hashes : control::payload {};
namespace blockchain {
struct mining_template {
    uint32_t version = 4;
    hash_digest previous_block_hash{};
    size_t height = 42;
    uint32_t bits = 1, min_time = 2, current_time = 3;
    uint64_t coinbase_value = 4, size_limit = 5, sigchecks_limit = 6;
    struct {
        std::vector<domain::message::transaction::const_ptr> txs;
    } selection;
};
}  // namespace blockchain
}  // namespace kth
