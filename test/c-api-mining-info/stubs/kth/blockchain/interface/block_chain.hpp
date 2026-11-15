// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <kth/capi/chain/mining_info.h>
#include <new>
#include <stdexcept>
#include <system_error>

namespace kth::blockchain {
enum class scenario { success, domain_error, allocation, exception, startup_allocation, startup_exception };
struct result {
    bool ok = false;
    kth_mining_info_t value{};
    explicit operator bool() const { return ok; }
    kth_mining_info_t const& operator*() const { return value; }
    std::error_code error() const { return std::make_error_code(std::errc::invalid_argument); }
};
struct block_chain {
    asio::io_context context;
    scenario mode;
    auto executor() {
        if (mode == scenario::startup_allocation) throw std::bad_alloc{};
        if (mode == scenario::startup_exception) throw std::runtime_error("startup failed");
        return context.get_executor();
    }
    asio::awaitable<result> fetch_mining_info() {
        if (mode == scenario::allocation) throw std::bad_alloc{};
        if (mode == scenario::exception) throw std::runtime_error("query failed");
        co_return result{mode == scenario::success, {42, 1.5, 3, 0, false, true, true}};
    }
};
}
