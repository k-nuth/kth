// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node/header_sync.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <exception>
#include <new>
#include <system_error>
#include <asio/co_spawn.hpp>
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
        auto created = cpp_t::create(bridge_url_cpp, network_cpp, capacity_cpp);
        if (!created) return kth::to_c_err(created.error());
        *out = created->release();
        return kth_ec_success;
    } catch (std::bad_alloc const&) {
        return kth_ec_out_of_memory;
    } catch (...) {
        return kth_ec_unhandled_exception;
    }
}


// Destructor

kth_error_code_t kth_node_header_sync_destruct(kth_header_sync_mut_t self) {
    if (self == nullptr) return kth_ec_success;
    // A pending run(), follow() or queued operation still uses it: refuse
    // rather than free it.
    if (!kth::cpp_ref<cpp_t>(self).settled()) return kth_ec_operation_failed;
    kth::del<cpp_t>(self);
    return kth_ec_success;
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
        // Held from here, before the operation is queued, until its handler
        // is called: the handle is not destroyed or reset under it, and may be
        // from the handler.
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.request_stop()) {
            co_return co_await bc.request_stop();
        }, [self, ctx, handler, report_failure, held = bc.admit()](std::exception_ptr failure, auto result) mutable {
            { auto const released = std::move(held); }
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), uint64_t(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), uint64_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

void kth_node_header_sync_async_request_refresh(kth_header_sync_const_t self, void* ctx, kth_node_header_sync_async_request_refresh_handler_t handler) {
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
        // Held from here, before the operation is queued, until its handler
        // is called: the handle is not destroyed or reset under it, and may be
        // from the handler.
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.request_refresh()) {
            co_return co_await bc.request_refresh();
        }, [self, ctx, handler, report_failure, held = bc.admit()](std::exception_ptr failure, auto result) mutable {
            { auto const released = std::move(held); }
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), uint64_t(*result));
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
    try {
        auto const s = kth::cpp_ref<cpp_t>(self).status();
        return kth::create_c_str(s);
    } catch (...) {
        return nullptr;
    }
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
        // Held from here, before the operation is queued, until its handler
        // is called: the handle is not destroyed or reset under it, and may be
        // from the handler.
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.run()) {
            co_return co_await bc.run();
        }, [self, ctx, handler, report_failure, held = bc.admit()](std::exception_ptr failure, auto result) mutable {
            { auto const released = std::move(held); }
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), uint64_t(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), uint64_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

void kth_node_header_sync_async_follow(kth_header_sync_mut_t self, void* ctx, kth_node_header_sync_async_follow_handler_t handler) {
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
        // Held from here, before the operation is queued, until its handler
        // is called: the handle is not destroyed or reset under it, and may be
        // from the handler.
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.follow()) {
            co_return co_await bc.follow();
        }, [self, ctx, handler, report_failure, held = bc.admit()](std::exception_ptr failure, auto result) mutable {
            { auto const released = std::move(held); }
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), uint64_t(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), uint64_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

kth_error_code_t kth_node_header_sync_reset(kth_header_sync_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).reset());
}

char* kth_node_header_sync_header_hex(kth_header_sync_const_t self, uint64_t height) {
    KTH_PRECONDITION(self != nullptr);
    try {
        auto const s = kth::cpp_ref<cpp_t>(self).header_hex(height);
        return kth::create_c_str(s);
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_header_hash(kth_header_sync_const_t self, uint64_t height) {
    KTH_PRECONDITION(self != nullptr);
    try {
        auto const s = kth::cpp_ref<cpp_t>(self).header_hash(height);
        return kth::create_c_str(s);
    } catch (...) {
        return nullptr;
    }
}


char* kth_node_header_sync_chain_state_snapshot(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).chain_state_snapshot());
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_runtime_settings(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).runtime_settings());
    } catch (...) {
        return nullptr;
    }
}

kth_bool_t kth_node_header_sync_configure_content(kth_header_sync_mut_t self, kth_bool_t blocks, kth_bool_t transactions) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).configure_content(kth::int_to_bool(blocks), kth::int_to_bool(transactions)));
}

char* kth_node_header_sync_block_metadata(kth_header_sync_const_t self, uint64_t height, kth_bool_t include_transactions) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).block_metadata(height, kth::int_to_bool(include_transactions)));
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_content_state(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).content_state());
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_observed_transactions(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).observed_transactions());
    } catch (...) {
        return nullptr;
    }
}

kth_bool_t kth_node_header_sync_add_peer(kth_header_sync_mut_t self, char const* url) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(url != nullptr);
    try {
        return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).add_peer(url));
    } catch (...) {
        return kth::bool_to_int(false);
    }
}

kth_bool_t kth_node_header_sync_configure_discovery(kth_header_sync_mut_t self, kth_bool_t enabled, kth_bool_t allow_private, kth_bool_t wss_only) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).configure_discovery(
        kth::int_to_bool(enabled), kth::int_to_bool(allow_private), kth::int_to_bool(wss_only)));
}

kth_size_t kth_node_header_sync_peer_count(kth_header_sync_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).peer_count();
}

char* kth_node_header_sync_peer_endpoint(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).peer_endpoint(at));
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_peer_source(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).peer_source(at));
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_peer_state(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).peer_state(at));
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_peer_agent(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).peer_agent(at));
    } catch (...) {
        return nullptr;
    }
}

char* kth_node_header_sync_peer_error(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    try {
        return kth::create_c_str(kth::cpp_ref<cpp_t>(self).peer_error(at));
    } catch (...) {
        return nullptr;
    }
}

uint64_t kth_node_header_sync_peer_height(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).peer_height(at);
}

uint64_t kth_node_header_sync_peer_announcements(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).peer_announcements(at);
}

kth_bool_t kth_node_header_sync_peer_connected(kth_header_sync_const_t self, kth_size_t at) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).peer_connected(at));
}

} // extern "C"
#endif // KTH_BROWSER_NODE
