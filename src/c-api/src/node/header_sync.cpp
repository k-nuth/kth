// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node/header_sync.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <new>
#include <system_error>
#include <asio/co_spawn.hpp>
#include <exception>
#include <new>
#include <kth/node/header_sync.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::node::header_sync;
} // namespace

#if defined(KTH_BROWSER_NODE)

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_node_header_sync_construct(char const* bridge_url, kth_network_t network, kth_size_t capacity, KTH_OUT_OWNED kth_header_sync_mut_t* out) {
    KTH_PRECONDITION(bridge_url != nullptr);
    KTH_PRECONDITION(out != nullptr);
    *out = nullptr;
    try {
        auto const bridge_url_cpp = std::string(bridge_url);
        auto const network_cpp = kth::network_to_cpp(network);
        auto const capacity_cpp = kth::sz(capacity);
        *out = kth::make_owned<cpp_t>(bridge_url_cpp, network_cpp, capacity_cpp);
        return kth_ec_success;
    } catch (std::bad_alloc const&) {
        return kth_ec_out_of_memory;
    } catch (...) {
        return kth_ec_unhandled_exception;
    }
}


// Destructor

void kth_node_header_sync_destruct(kth_header_sync_mut_t self) {
    kth::del<cpp_t>(self);
}


// Getters

void kth_node_header_sync_async_request_stop(kth_header_sync_const_t self, void* ctx, kth_node_header_sync_async_request_stop_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, uint64_t{});
    };
    try {
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.request_stop()) {
            co_return co_await bc.request_stop();
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), static_cast<uint64_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), uint64_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

uint64_t kth_node_header_sync_last_height(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).last_height();
}

kth_bool_t kth_node_header_sync_running(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).running());
}

kth_bool_t kth_node_header_sync_idle(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).idle());
}

char* kth_node_header_sync_status(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).status();
    return kth::create_c_str(s);
}

kth_size_t kth_node_header_sync_index_memory_bytes(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).index_memory_bytes();
}


// Operations

void kth_node_header_sync_async_run(kth_header_sync_mut_t self, void* ctx, kth_node_header_sync_async_run_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, uint64_t{});
    };
    try {
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.run()) {
            co_return co_await bc.run();
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), static_cast<uint64_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), uint64_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

void kth_node_header_sync_reset(kth_header_sync_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
#endif // KTH_BROWSER_NODE
