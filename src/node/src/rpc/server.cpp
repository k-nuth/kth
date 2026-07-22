// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/server.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/address.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <llhttp.h>
#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
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
    return 0;
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

server::server(rpc_settings const& settings, blockchain::block_chain& chain)
    : settings_(settings)
    , chain_(chain)
    , jobs_(settings.gbt_cache_size, settings.gbt_store_time) {
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

::asio::awaitable<void> server::serve_connection(::asio::ip::tcp::socket socket) {
    std::array<char, 8192> buffer;
    for (;;) {
        parse_ctx ctx;
        llhttp_t parser;
        llhttp_settings_t settings;
        init_parser(parser, settings, ctx);

        while ( ! ctx.complete) {
            auto [ec, n] = co_await socket.async_read_some(
                ::asio::buffer(buffer), ::asio::as_tuple(::asio::use_awaitable));
            if (ec || n == 0) {
                co_return; // client closed or read error
            }
            if (llhttp_execute(&parser, buffer.data(), n) != HPE_OK) {
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

::asio::awaitable<void> server::run(::asio::any_io_executor executor) {
    using ::asio::ip::tcp;

    std::error_code ec;
    auto const address = ::asio::ip::make_address(settings_.bind, ec);
    if (ec) {
        spdlog::error("[rpc] invalid bind address '{}': {}", settings_.bind, ec.message());
        co_return;
    }

    tcp::endpoint const endpoint(address, settings_.port);
    tcp::acceptor acceptor(executor);
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("[rpc] failed to open acceptor: {}", ec.message());
        co_return;
    }
    acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("[rpc] failed to bind {}:{}: {}", settings_.bind, settings_.port,
            ec.message());
        co_return;
    }
    acceptor.listen(tcp::socket::max_listen_connections, ec);
    if (ec) {
        spdlog::error("[rpc] failed to listen: {}", ec.message());
        co_return;
    }

    spdlog::info("[rpc] JSON-RPC server listening on {}:{}", settings_.bind, settings_.port);

    for (;;) {
        auto [aec, socket] = co_await acceptor.async_accept(
            ::asio::as_tuple(::asio::use_awaitable));
        if (aec) {
            if (aec == ::asio::error::operation_aborted) {
                co_return;
            }
            continue;
        }
        ::asio::co_spawn(executor, serve_connection(std::move(socket)), ::asio::detached);
    }
}

} // namespace kth::node::rpc
