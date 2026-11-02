// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/server.hpp>

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <exception>
#include <memory>
#include <unordered_set>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/address.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/this_coro.hpp>
#include <asio/cancellation_state.hpp>

#include <llhttp.h>
#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/node/detail/scope_action.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::node::rpc {

namespace {

// ---- HTTP request accumulation (llhttp callbacks) -------------------------

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

struct parse_ctx {
    std::string field;
    std::string value;
    bool reading_value = false;
    std::string authorization;
    std::string body;
    bool complete = false;
    unsigned method = 0;
    bool keep_alive = true;

    void commit_header() {
        if (iequals(field, "authorization")) {
            authorization = value;
        }
        field.clear();
        value.clear();
    }
};

int on_header_field(llhttp_t* p, char const* at, std::size_t len) {
    auto* c = static_cast<parse_ctx*>(p->data);
    if (c->reading_value) {
        c->commit_header();
        c->reading_value = false;
    }
    c->field.append(at, len);
    return 0;
}

int on_header_value(llhttp_t* p, char const* at, std::size_t len) {
    auto* c = static_cast<parse_ctx*>(p->data);
    c->value.append(at, len);
    c->reading_value = true;
    return 0;
}

int on_headers_complete(llhttp_t* p) {
    auto* c = static_cast<parse_ctx*>(p->data);
    if (c->reading_value) {
        c->commit_header();
        c->reading_value = false;
    }
    return 0;
}

int on_body(llhttp_t* p, char const* at, std::size_t len) {
    static_cast<parse_ctx*>(p->data)->body.append(at, len);
    return 0;
}

int on_message_complete(llhttp_t* p) {
    auto* c = static_cast<parse_ctx*>(p->data);
    c->complete = true;
    c->method = llhttp_get_method(p);
    c->keep_alive = llhttp_should_keep_alive(p) != 0;
    // Stop at the end of this request: bytes after it belong to the next
    // request a client pipelined on the same connection.
    return HPE_PAUSED;
}

void init_parser(llhttp_t& parser, llhttp_settings_t& settings, parse_ctx& ctx) {
    llhttp_settings_init(&settings);
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    llhttp_init(&parser, HTTP_REQUEST, &settings);
    parser.data = &ctx;
}

// ---- HTTP response --------------------------------------------------------

::asio::awaitable<void> write_response(
    ::asio::ip::tcp::socket& socket,
    int status,
    std::string_view reason,
    std::string_view body,
    bool keep_alive,
    std::string_view extra_headers = {}) {

    std::string out;
    out.reserve(body.size() + 160);
    out += "HTTP/1.1 ";
    out += std::to_string(status);
    out += ' ';
    out += reason;
    out += "\r\nContent-Type: application/json\r\nContent-Length: ";
    out += std::to_string(body.size());
    out += "\r\nConnection: ";
    out += keep_alive ? "keep-alive" : "close";
    out += "\r\n";
    out += extra_headers;
    out += "\r\n";
    out += body;

    co_await ::asio::async_write(socket, ::asio::buffer(out),
        ::asio::as_tuple(::asio::use_awaitable));
}

std::string random_token() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i) {
        token.push_back(hex[dist(gen)]);
    }
    return token;
}

} // namespace

server::server(rpc_settings const& settings, blockchain::block_chain& chain,
    ::asio::any_io_executor executor)
    : settings_(settings)
    , chain_(chain)
    , jobs_(settings.gbt_cache_size, settings.gbt_store_time)
    , owner_(::asio::make_strand(std::move(executor)))
    , stop_requests_(owner_, 1)
    , listening_(owner_, 1) {
    register_builtin_methods(dispatcher_);

    if ( ! settings_.user.empty() || ! settings_.password.empty()) {
        expected_credentials_ = settings_.user + ":" + settings_.password;
    } else {
        // bitcoind-style cookie: "__cookie__:<random>" written to ./.cookie.
        expected_credentials_ = "__cookie__:" + random_token();
        std::ofstream cookie(".cookie", std::ios::trunc);
        cookie << expected_credentials_;
        spdlog::info("[rpc] using generated auth cookie (./.cookie)");
    }
}

bool server::authorized(std::string_view authorization_header) const {
    constexpr std::string_view prefix = "Basic ";
    if ( ! authorization_header.starts_with(prefix)) {
        return false;
    }
    data_chunk decoded;
    if ( ! decode_base64(decoded, authorization_header.substr(prefix.size()))) {
        return false;
    }
    std::string_view const got(
        reinterpret_cast<char const*>(decoded.data()), decoded.size());
    return got == expected_credentials_;
}

::asio::awaitable<void> server::serve_connection(::asio::ip::tcp::socket& socket) {
    std::array<char, 8192> buffer;
    // Bytes read past the end of the previous request: the start of the next
    // one when a client pipelines requests on a keep-alive connection.
    std::string pending;
    for (;;) {
        parse_ctx ctx;
        llhttp_t parser;
        llhttp_settings_t settings;
        init_parser(parser, settings, ctx);

        while ( ! ctx.complete) {
            std::string_view input = pending;
            if (input.empty()) {
                auto [ec, n] = co_await socket.async_read_some(
                    ::asio::buffer(buffer), ::asio::as_tuple(::asio::use_awaitable));
                if (ec || n == 0) {
                    co_return; // client closed or read error
                }
                input = std::string_view(buffer.data(), n);
            }
            auto const status = llhttp_execute(&parser, input.data(), input.size());
            if (status == HPE_PAUSED) {
                // on_message_complete paused right after this request.
                pending = std::string(llhttp_get_error_pos(&parser), input.data() + input.size());
            } else if (status == HPE_OK) {
                pending.clear();
            } else {
                co_await write_response(socket, 400, "Bad Request",
                    R"({"result":null,"error":{"code":-32700,"message":"Parse error"},"id":null})",
                    false);
                co_return;
            }
        }

        if ( ! authorized(ctx.authorization)) {
            co_await write_response(socket, 401, "Unauthorized",
                R"({"result":null,"error":{"code":-1,"message":"unauthorized"},"id":null})",
                false, "WWW-Authenticate: Basic realm=\"kth\"\r\n");
            co_return;
        }

        if (ctx.method != HTTP_POST) {
            co_await write_response(socket, 405, "Method Not Allowed",
                R"({"result":null,"error":{"code":-32600,"message":"Only POST is supported"},"id":null})",
                false);
            co_return;
        }

        method_context mctx{chain_, jobs_};
        std::string const response = co_await dispatcher_.handle(mctx, ctx.body);
        co_await write_response(socket, 200, "OK", response, ctx.keep_alive);
        if ( ! ctx.keep_alive) {
            co_return;
        }
    }
}

void server::stop() {
    // Startup may have a waiter even if run() will never be entered.
    // This channel is concurrent; socket shutdown still belongs to owner_.
    listening_.close();
    (void)stop_requests_.try_send(std::error_code{}, 1);
}

::asio::awaitable<void> server::run() {
    // Also wake a startup waiter if cancellation prevents run_owned starting.
    detail::scope_action close_startup([this] { listening_.close(); }, true);
    co_await ::asio::co_spawn(owner_, run_owned(), ::asio::use_awaitable);
}

::asio::awaitable<uint16_t> server::wait_until_listening() {
    co_return co_await listening_.async_receive(::asio::use_awaitable);
}

::asio::awaitable<void> server::run_owned() {
    using ::asio::ip::tcp;
    using namespace ::asio::experimental::awaitable_operators;

    std::error_code ec;
    auto const address = ::asio::ip::make_address(settings_.bind, ec);
    if (ec) {
        spdlog::error("[rpc] invalid bind address '{}': {}", settings_.bind, ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    tcp::endpoint const endpoint(address, settings_.port);
    tcp::acceptor acceptor(owner_);
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("[rpc] failed to open acceptor: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("[rpc] failed to bind {}:{}: {}", settings_.bind, settings_.port,
            ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.listen(tcp::socket::max_listen_connections, ec);
    if (ec) {
        spdlog::error("[rpc] failed to listen: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    auto const bound = acceptor.local_endpoint(ec);
    if (ec) {
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    (void)listening_.try_send(std::error_code{}, bound.port());
    spdlog::info("[rpc] JSON-RPC server listening on {}:{}", settings_.bind, bound.port());

    // All mutable listener/connection state has one owner. At most 64 children
    // are admitted, and every completion is consumed before this scope exits.
    constexpr std::size_t capacity = 64;
    ::asio::experimental::channel<void(std::error_code, std::exception_ptr)> ended(owner_, capacity);
    std::unordered_set<std::shared_ptr<tcp::socket>> connections;
    std::size_t pending = 0;
    bool stopping = false;
    ::asio::steady_timer accept_retry(owner_);
    std::exception_ptr failure;
    auto close_owned = [&] {
        stopping = true;
        std::error_code ignored;
        acceptor.close(ignored);
        try {
            accept_retry.cancel();
        } catch (...) {
            // A timer-service error must not bypass closing or joining children.
            if (!failure) failure = std::current_exception();
        }
        for (auto const& socket : connections) {
            socket->close(ignored);
        }
    };
    auto reap = [&]() -> ::asio::awaitable<void> {
        auto error = co_await ended.async_receive(::asio::use_awaitable);
        --pending;
        if (error && !failure) failure = error;
    };
    auto accept_connections = [&]() -> ::asio::awaitable<void> {
        try {
            while (!stopping) {
                if (pending == capacity) {
                    co_await reap();
                    continue;
                }
                tcp::socket socket(owner_);
                auto [aec] = co_await acceptor.async_accept(socket,
                    ::asio::as_tuple(::asio::use_awaitable));
                if (aec) {
                    if (stopping || aec == ::asio::error::operation_aborted) break;
                    spdlog::warn("[rpc] accept failed: {}; retrying", aec.message());
                    // A resource shortage is not the end of the service. This
                    // owner-local deadline avoids a busy loop and is cancelled
                    // by the same stop event that closes the listener.
                    accept_retry.expires_after(std::chrono::milliseconds(100));
                    (void)co_await accept_retry.async_wait(::asio::as_tuple(::asio::use_awaitable));
                    continue;
                }
                if (stopping) {
                    std::error_code ignored;
                    socket.close(ignored);
                    break;
                }
                auto child = std::make_shared<tcp::socket>(std::move(socket));
                connections.insert(child);
                ++pending;
                // co_spawn's completion adapter only publishes an owned result.
                // It runs on the same strand; its captures cannot outlive the
                // drain below, even for partial HTTP reads or handler errors.
                try {
                    ::asio::co_spawn(owner_, serve_connection(*child),
                        [&, child](std::exception_ptr error) {
                            connections.erase(child);
                            (void)ended.try_send(std::error_code{}, error);
                        });
                } catch (...) {
                    --pending;
                    connections.erase(child);
                    throw;
                }
            }
        } catch (...) {
            failure = std::current_exception();
        }
        close_owned();
        stop();
        co_await ::asio::this_coro::reset_cancellation_state(::asio::disable_cancellation());
        while (pending != 0) co_await reap();
    };
    auto receive_stop = [&]() -> ::asio::awaitable<void> {
        try {
            (void)co_await stop_requests_.async_receive(::asio::use_awaitable);
        } catch (...) {
            close_owned();
            throw;
        }
        spdlog::info("[rpc] Stop received; closing listener and {} connections", connections.size());
        close_owned();
    };
    co_await (accept_connections() && receive_stop());
    spdlog::info("[rpc] Listener stopped; all connections joined");
    if (failure) std::rethrow_exception(failure);
}

} // namespace kth::node::rpc
