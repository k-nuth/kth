// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node/wallet.h>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <system_error>

#include <asio/co_spawn.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/wallet/electrum.hpp>
#include <kth/node/wallet/service.hpp>

#if ! defined(__EMSCRIPTEN__)

namespace {

namespace wallet = kth::node::wallet;

// The service, and the Electrum-protocol answers in front of it (no
// listener). Destroyed in reverse: the answers go before the service, and
// both only once every handler given the handle has returned.
struct wallet_handle {
    std::unique_ptr<wallet::service> service;
    std::unique_ptr<wallet::electrum_server> answers;
    std::mutex mutex;
    std::condition_variable idle;
    size_t pending = 0;

    void begin() {
        std::lock_guard<std::mutex> guard(mutex);
        ++pending;
    }

    void end() {
        std::lock_guard<std::mutex> guard(mutex);
        --pending;
        idle.notify_all();
    }

    // A stopped service ends what it runs at once; one stuck past a minute
    // is not waited for.
    void wait_idle() {
        std::unique_lock<std::mutex> guard(mutex);
        (void)idle.wait_for(guard, std::chrono::minutes(1), [this] { return pending == 0; });
    }
};

wallet_handle& handle_of(void const* self) {
    return *static_cast<wallet_handle*>(const_cast<void*>(self));
}

kth_error_code_t adopt(std::unique_ptr<wallet::service> service, kth::domain::config::network network,
        kth_wallet_mut_t* out) {
    auto created = std::make_unique<wallet_handle>();
    created->answers = std::make_unique<wallet::electrum_server>(*service, network);
    created->service = std::move(service);
    *out = created.release();
    return kth_ec_success;
}

} // namespace

extern "C" {

kth_error_code_t kth_node_wallet_construct_from_chain(kth_chain_t chain, kth_network_t network, uint32_t birthday,
        KTH_OUT_OWNED kth_wallet_mut_t* out) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out != nullptr);
    *out = nullptr;
    try {
        auto& own = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        wallet::chain_settings settings;
        settings.birthday = birthday;
        auto const net = kth::network_to_cpp(network);
        return adopt(wallet::service::from_chain(own, net, settings), net, out);
    } catch (std::bad_alloc const&) {
        return kth_ec_out_of_memory;
    } catch (...) {
        return kth_ec_unhandled_exception;
    }
}

void kth_node_wallet_destruct(kth_wallet_mut_t self) {
    if (self == nullptr) return;
    auto* owned = static_cast<wallet_handle*>(self);
    owned->answers->stop();
    owned->service->stop();
    // A run or a request under way ends with the service stopped, and its
    // handler gets the handle whole.
    owned->wait_idle();
    delete owned;
}

void kth_node_wallet_async_run(kth_wallet_mut_t self, void* ctx, kth_node_wallet_run_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    try {
        auto& owned = handle_of(self);
        owned.begin();
        try {
            ::asio::co_spawn(owned.service->executor(), owned.service->run(),
                [self, ctx, handler, &owned](std::exception_ptr failure, std::error_code ec) {
                    handler(self, ctx, failure ? kth_ec_unhandled_exception : kth::to_c_err(ec));
                    owned.end();
                });
        } catch (...) {
            owned.end();
            throw;
        }
    } catch (std::bad_alloc const&) {
        handler(self, ctx, kth_ec_out_of_memory);
    } catch (...) {
        handler(self, ctx, kth_ec_unhandled_exception);
    }
}

void kth_node_wallet_stop(kth_wallet_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    handle_of(self).service->stop();
}

char* kth_node_wallet_status(kth_wallet_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    try {
        auto const now = handle_of(self).service->current();
        kth::node::rpc::writer out;
        auto const height = [&out](std::string_view key, std::optional<uint32_t> const& value) {
            out.key(key);
            if (value) {
                out.value(uint64_t(*value));
            } else {
                out.value_null();
            }
        };
        out.begin_object();
        out.field("source", std::string_view(now.source));
        out.field("header_height", uint64_t(now.header_height));
        height("tip", now.tip);
        height("input_prefix_tip", now.input_prefix_tip);
        out.field("peers", uint64_t(now.peers));
        out.field("synced", now.synced);
        out.key("transaction_keys");
        if (now.transaction_keys) {
            out.value(*now.transaction_keys);
        } else {
            out.value_null();
        }
        out.field("bytes_received", uint64_t(now.bytes_received));
        out.field("bytes_sent", uint64_t(now.bytes_sent));
        out.field("blocks_downloaded", uint64_t(now.blocks_downloaded));
        out.field("filter_bytes", uint64_t(now.filter_bytes));
        out.field("error", std::string_view(now.error));
        out.end_object();
        auto const json = out.str();
        return kth::create_c_str(json);
    } catch (...) {
        return nullptr;
    }
}

void kth_node_wallet_async_request(kth_wallet_mut_t self, char const* request, void* ctx,
        kth_node_wallet_answer_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(request != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    try {
        auto& owned = handle_of(self);
        owned.begin();
        try {
            ::asio::co_spawn(owned.service->executor(), owned.answers->answer(std::string(request)),
                [self, ctx, handler, &owned](std::exception_ptr failure, std::string answer) {
                    if (failure) {
                        handler(self, ctx, kth_ec_unhandled_exception, nullptr);
                    } else {
                        handler(self, ctx, kth_ec_success, kth::create_c_str(answer));
                    }
                    owned.end();
                });
        } catch (...) {
            owned.end();
            throw;
        }
    } catch (std::bad_alloc const&) {
        handler(self, ctx, kth_ec_out_of_memory, nullptr);
    } catch (...) {
        handler(self, ctx, kth_ec_unhandled_exception, nullptr);
    }
}

} // extern "C"

#endif // ! __EMSCRIPTEN__
