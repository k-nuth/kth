// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/grpc_rpc/transport.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include <agrpc/asio_grpc.hpp>
#include <grpcpp/grpcpp.h>

#include <asio/co_spawn.hpp>
#include <asio/ip/address.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/rpc/v1/node.grpc.pb.h>

namespace kth::node::grpc_rpc {

namespace {

namespace v1 = ::kth::rpc::v1;

// NOTIFY_WHEN_DONE lets a call learn that its client went away (or the server is
// shutting down) while the handler is still working.
struct call_traits : agrpc::DefaultServerRPCTraits {
    static constexpr bool NOTIFY_WHEN_DONE = true;
};

template <auto Request>
using call = agrpc::ServerRPC<Request, call_traits>;

using get_chain_info_call = call<&v1::NodeService::AsyncService::RequestGetChainInfo>;
using get_block_call = call<&v1::NodeService::AsyncService::RequestGetBlock>;
using get_transaction_call = call<&v1::NodeService::AsyncService::RequestGetTransaction>;

grpc::StatusCode to_grpc(status_code code) {
    switch (code) {
        case status_code::invalid_argument: return grpc::StatusCode::INVALID_ARGUMENT;
        case status_code::not_found: return grpc::StatusCode::NOT_FOUND;
        case status_code::resource_exhausted: return grpc::StatusCode::RESOURCE_EXHAUSTED;
        case status_code::unavailable: return grpc::StatusCode::UNAVAILABLE;
        case status_code::aborted: return grpc::StatusCode::ABORTED;
        case status_code::internal: return grpc::StatusCode::INTERNAL;
    }
    return grpc::StatusCode::INTERNAL;
}

grpc::Status to_grpc(rpc_status const& status) {
    return grpc::Status(to_grpc(status.code), status.message);
}

// The comparison time does not depend on where the strings first differ.
bool equal_constant_time(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

int clamp_to_int(std::size_t value) {
    return static_cast<int>(std::min<std::size_t>(value, std::numeric_limits<int>::max()));
}

} // namespace

struct transport::impl {
    impl(transport& owner, std::string expected_authorization)
        : owner(owner)
        , expected_authorization(std::move(expected_authorization))
    {}

    // The server goes first: it must be down before the context that serves its
    // completion queue is destroyed.
    ~impl() {
        server.reset();
        grpc_context.reset();
    }

    transport& owner;
    std::string const expected_authorization;
    v1::NodeService::AsyncService service;
    std::unique_ptr<agrpc::GrpcContext> grpc_context;
    std::unique_ptr<grpc::Server> server;

    [[nodiscard]]
    bool authorized(grpc::ServerContext const& context) const {
        auto const& metadata = context.client_metadata();
        auto const range = metadata.equal_range("authorization");
        for (auto it = range.first; it != range.second; ++it) {
            std::string_view const value(it->second.data(), it->second.size());
            if (equal_constant_time(value, expected_authorization)) {
                return true;
            }
        }
        return false;
    }

    // Take one of max_in_flight slots, or none. Lock free: this is the admission
    // decision for every call, made before any work is started.
    [[nodiscard]]
    bool admit() {
        auto const taken = owner.in_flight_.fetch_add(1, std::memory_order_acq_rel);
        if (taken >= owner.options_.max_in_flight) {
            owner.in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }
        return true;
    }

    struct slot {
        explicit slot(std::atomic<std::size_t>& counter) : counter(counter) {}
        ~slot() { counter.fetch_sub(1, std::memory_order_acq_rel); }
        slot(slot const&) = delete;
        slot& operator=(slot const&) = delete;
        std::atomic<std::size_t>& counter;
    };

    // A handler that throws is a failed call, not a failed server: the exception is
    // turned into a status here, before it can reach the race below.
    template <typename Request, typename Response>
    static ::asio::awaitable<result<Response>> invoke(handler<Request, Response> const& run,
        Request const& request)
    {
        try {
            co_return co_await run(request);
        } catch (std::exception const& error) {
            spdlog::error("[grpc] handler failed: {}", error.what());
        } catch (...) {
            spdlog::error("[grpc] handler failed with an unknown exception");
        }
        co_return std::unexpected(rpc_status{status_code::internal, "internal error"});
    }

    template <typename Call, typename Request, typename Response>
    ::asio::awaitable<void> serve(Call& rpc, Request& request,
        handler<Request, Response> const& run)
    {
        using namespace ::asio::experimental::awaitable_operators;

        if ( ! authorized(rpc.context())) {
            co_await rpc.finish_with_error(
                grpc::Status(grpc::StatusCode::UNAUTHENTICATED, "missing or invalid authorization"),
                ::asio::use_awaitable);
            co_return;
        }

        if ( ! admit()) {
            co_await rpc.finish_with_error(
                grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                    "server is at its in-flight limit; retry later"),
                ::asio::use_awaitable);
            co_return;
        }
        slot const admitted(owner.in_flight_);

        if ( ! run) {
            co_await rpc.finish_with_error(
                grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "method has no handler"),
                ::asio::use_awaitable);
            co_return;
        }

        std::optional<result<Response>> outcome;
        bool call_ended = false;
        try {
            // The handler races the call's own end: a client that cancels or
            // disappears, or a server shutdown past its grace, cancels the handler
            // instead of leaving it to finish work nobody will read.
            auto raced = co_await (invoke<Request, Response>(run, request) || rpc.wait_for_done(::asio::use_awaitable));
            if (raced.index() == 0) {
                outcome.emplace(std::move(std::get<0>(raced)));
            } else {
                call_ended = true;
            }
        } catch (std::exception const& error) {
            spdlog::error("[grpc] handler failed: {}", error.what());
            outcome.emplace(std::unexpected(rpc_status{status_code::internal, "internal error"}));
        } catch (...) {
            spdlog::error("[grpc] handler failed with an unknown exception");
            outcome.emplace(std::unexpected(rpc_status{status_code::internal, "internal error"}));
        }

        if (call_ended || ! outcome) {
            co_return;
        }

        if ( ! *outcome) {
            co_await rpc.finish_with_error(to_grpc(outcome->error()), ::asio::use_awaitable);
            co_return;
        }

        auto const size = outcome->value().ByteSizeLong();
        if (size > owner.options_.max_response_bytes) {
            co_await rpc.finish_with_error(
                grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                    "response of " + std::to_string(size) + " bytes exceeds the " +
                    std::to_string(owner.options_.max_response_bytes) + " byte limit"),
                ::asio::use_awaitable);
            co_return;
        }
        co_await rpc.finish(outcome->value(), grpc::Status::OK, ::asio::use_awaitable);
    }

    template <typename Call, typename Request, typename Response>
    ::asio::awaitable<void> register_method(handler<Request, Response> const& run) {
        co_await agrpc::register_awaitable_rpc_handler<Call>(
            *grpc_context, service,
            [this, &run](Call& rpc, typename Call::Request& request) {
                return serve<Call, Request, Response>(rpc, request, run);
            },
            ::asio::use_awaitable);
    }

    ::asio::awaitable<void> serve_all() {
        using namespace ::asio::experimental::awaitable_operators;
        auto const& table = owner.handlers_;
        co_await (
            register_method<get_chain_info_call>(table.get_chain_info) &&
            register_method<get_block_call>(table.get_block) &&
            register_method<get_transaction_call>(table.get_transaction));
    }
};

transport::transport(transport_options options, handlers service)
    : options_(std::move(options))
    , handlers_(std::move(service))
{}

transport::~transport() {
    stop();
    join();
}

bool transport::set_credentials(std::string user, std::string password) {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (state_ != state::created) {
        return false;
    }
    options_.user = std::move(user);
    options_.password = std::move(password);
    return true;
}

std::expected<std::uint16_t, std::string> transport::start() {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (state_ == state::stopped) {
        return std::unexpected(std::string("the gRPC transport was stopped before it started"));
    }
    if (state_ == state::running) {
        return std::unexpected(std::string("the gRPC transport is already started"));
    }

    // Everything refusable is refused before a socket exists.
    if (options_.user.empty() && options_.password.empty()) {
        return std::unexpected(std::string("the gRPC transport requires credentials"));
    }
    if (options_.max_in_flight == 0) {
        return std::unexpected(std::string("max_in_flight must be at least 1"));
    }
    std::error_code address_error;
    auto const address = ::asio::ip::make_address(options_.bind, address_error);
    if (address_error) {
        return std::unexpected("rpc.bind '" + options_.bind +
            "' is not an IP address literal: " + address_error.message());
    }
    if ( ! address.is_loopback()) {
        return std::unexpected("rpc.bind '" + options_.bind +
            "' is not a loopback address: the gRPC backend is plaintext and refuses to expose "
            "chain reads and credentials beyond this host");
    }

    auto const credentials = options_.user + ":" + options_.password;
    auto expected = "Basic " + encode_base64(byte_span(
        reinterpret_cast<uint8_t const*>(credentials.data()), credentials.size()));

    auto built = std::make_unique<impl>(*this, std::move(expected));

    auto const endpoint = (address.is_v6() ? "[" + options_.bind + "]" : options_.bind) +
        ":" + std::to_string(options_.port);
    int selected_port = 0;
    grpc::ServerBuilder builder;
    // Without this a second node could silently share the port on Linux.
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    builder.SetMaxReceiveMessageSize(clamp_to_int(options_.max_request_bytes));
    builder.SetMaxSendMessageSize(clamp_to_int(options_.max_response_bytes));
    builder.AddListeningPort(endpoint, grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&built->service);
    built->grpc_context = std::make_unique<agrpc::GrpcContext>(builder.AddCompletionQueue());
    built->server = builder.BuildAndStart();
    if ( ! built->server || selected_port == 0) {
        return std::unexpected("failed to listen on " + endpoint + " (is the port in use?)");
    }

    impl_ = std::move(built);
    port_ = static_cast<std::uint16_t>(selected_port);
    state_ = state::running;

    ::asio::co_spawn(*impl_->grpc_context, impl_->serve_all(), [](std::exception_ptr error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const& e) {
                spdlog::error("[grpc] service registration ended with an error: {}", e.what());
            } catch (...) {
                spdlog::error("[grpc] service registration ended with an unknown error");
            }
        }
    });

    grpc_thread_ = std::thread([this] {
        impl_->grpc_context->run();
        // Normally stop() got here first. If the context ran dry on its own, make
        // the controller leave its wait too, so join() cannot hang.
        {
            std::lock_guard<std::mutex> const done(mutex_);
            state_ = state::stopped;
        }
        stop_cv_.notify_all();
    });

    controller_thread_ = std::thread([this] {
        {
            std::unique_lock<std::mutex> wait(mutex_);
            stop_cv_.wait(wait, [this] { return state_ == state::stopped; });
        }
        // Blocks until calls drain or the grace period passes and they are
        // cancelled: on this thread, never on a node executor.
        impl_->server->Shutdown(std::chrono::system_clock::now() + options_.shutdown_grace);
    });

    spdlog::info("[grpc] listening on {} (plaintext, loopback only, authenticated)", endpoint);
    return port_;
}

void transport::stop() {
    {
        std::lock_guard<std::mutex> const lock(mutex_);
        if (state_ == state::stopped) {
            return;
        }
        state_ = state::stopped;
    }
    stop_cv_.notify_all();
}

void transport::join() {
    std::lock_guard<std::mutex> const joining(join_mutex_);
    if (controller_thread_.joinable()) {
        controller_thread_.join();
    }
    if (grpc_thread_.joinable()) {
        grpc_thread_.join();
    }
}

std::uint16_t transport::port() const {
    std::lock_guard<std::mutex> const lock(mutex_);
    return port_;
}

std::size_t transport::in_flight() const {
    return in_flight_.load(std::memory_order_acquire);
}

} // namespace kth::node::grpc_rpc
