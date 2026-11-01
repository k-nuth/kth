// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/template_provider.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/channel.hpp>
#include <asio/ip/address.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <spdlog/spdlog.h>

#include <kth/node/detail/scope_action.hpp>
#include <kth/node/sv2/connection.hpp>

namespace kth::node::sv2 {

namespace {

// The only protocol version defined so far (sv2-spec 3.6.1).
constexpr uint16_t protocol_version = 2;

template <typename Message>
data_chunk frame(Message const& message) {
    auto const payload = to_data_chunk(message);
    auto bytes = to_data_chunk(message_header{0, Message::message_type,
        static_cast<uint32_t>(payload.size())});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

// A payload is valid only if it is exactly one message, with no bytes left.
template <typename Message>
std::optional<Message> parse(byte_span payload) {
    byte_reader source(payload);
    auto message = Message::from_data(source);
    if ( ! message || ! source.is_exhausted()) {
        return std::nullopt;
    }
    return std::move(*message);
}

} // namespace

template_provider_session::result template_provider_session::close() {
    state_ = state::closed;
    return {{}, true};
}

template_provider_session::result template_provider_session::process(
    message_header const& header, byte_span payload) {
    if (state_ == state::closed || header.msg_length > max_payload_size
        || header.msg_length != payload.size()) {
        return close();
    }

    // SetupConnection must be the client's first message (sv2-spec 3.6.1).
    if (state_ == state::awaiting_setup) {
        if (header.extension_type != 0 || header.msg_type != setup_connection::message_type) {
            return close();
        }
        auto const setup = parse<setup_connection>(payload);
        if ( ! setup) {
            return close();
        }

        setup_connection_error error;
        if (setup->protocol != setup_connection::protocol_template_distribution) {
            error.error_code = "unsupported-protocol";
        } else if (setup->min_version > protocol_version || setup->max_version < protocol_version) {
            error.error_code = "protocol-version-mismatch";
        } else if (setup->flags != 0) {
            // No Template Distribution flags are defined (sv2-spec 7.1), so
            // every flag the client requires is one this server lacks.
            error.flags = setup->flags;
            error.error_code = "unsupported-feature-flags";
        }
        if ( ! error.error_code.empty()) {
            state_ = state::closed;
            return {frame(error), true};
        }
        state_ = state::ready;
        return {frame(setup_connection_success{protocol_version, 0}), false};
    }

    // Messages of an extension this server does not implement are discarded
    // (sv2-spec 3.2.1). Template Distribution messages never set channel_msg,
    // so on extension 0 that bit marks a malformed frame.
    if (header.extension() != 0) {
        return {};
    }
    if (header.channel_msg()) {
        return close();
    }

    // The client must send its constraints right after setup (sv2-spec 07):
    // every template sent to it depends on them.
    if ( ! constraints_ && header.msg_type != coinbase_output_constraints::message_type) {
        return close();
    }

    switch (header.msg_type) {
        case coinbase_output_constraints::message_type: {
            auto const constraints = parse<coinbase_output_constraints>(payload);
            if ( ! constraints) {
                return close();
            }
            constraints_ = *constraints;
            return {};
        }
        case request_transaction_data::message_type: {
            auto const request = parse<request_transaction_data>(payload);
            if ( ! request) {
                return close();
            }
            // No template has been sent yet, so no id can name one.
            return {frame(request_transaction_data_error{request->template_id,
                "template-id-not-found"}), false};
        }
        case submit_solution::message_type: {
            auto const solution = parse<submit_solution>(payload);
            if ( ! solution) {
                return close();
            }
            spdlog::debug("[sv2] Ignoring a solution for unknown template {}",
                solution->template_id);
            return {};
        }
        default:
            // A message only a server sends, or one the protocol does not define.
            return close();
    }
}

template_provider::template_provider(sv2_settings const& settings,
    ::asio::any_io_executor executor)
    : settings_(settings)
    , owner_(::asio::make_strand(executor))
    , stop_requests_(owner_, 1)
    , listening_(owner_, 1)
{}

::asio::awaitable<void> template_provider::serve_connection(::asio::ip::tcp::socket& socket) {
    template_provider_session session;
    std::array<uint8_t, message_header::size> header_bytes{};
    for (;;) {
        // EOF, a partial frame or a read error ends only this connection.
        auto [header_error, header_size] = co_await ::asio::async_read(socket,
            ::asio::buffer(header_bytes), ::asio::as_tuple(::asio::use_awaitable));
        if (header_error) {
            co_return;
        }
        auto const header = from_data_chunk<message_header>(header_bytes);
        if ( ! header || header->msg_length > template_provider_session::max_payload_size) {
            co_return;
        }

        data_chunk payload(header->msg_length);
        auto [payload_error, payload_size] = co_await ::asio::async_read(socket,
            ::asio::buffer(payload), ::asio::as_tuple(::asio::use_awaitable));
        if (payload_error) {
            co_return;
        }

        auto const result = session.process(*header, payload);
        if ( ! result.response.empty()) {
            auto [write_error, written] = co_await ::asio::async_write(socket,
                ::asio::buffer(result.response), ::asio::as_tuple(::asio::use_awaitable));
            if (write_error) {
                co_return;
            }
        }
        if (result.close) {
            co_return;
        }
    }
}

void template_provider::stop() {
    // Startup may have a waiter even if run() will never be entered.
    // This channel is concurrent; socket shutdown still belongs to owner_.
    listening_.close();
    (void)stop_requests_.try_send(std::error_code{}, 1);
}

::asio::awaitable<void> template_provider::run() {
    // Also wake a startup waiter if cancellation prevents run_owned starting.
    detail::scope_action close_startup([this] { listening_.close(); }, true);
    co_await ::asio::co_spawn(owner_, run_owned(), ::asio::use_awaitable);
}

::asio::awaitable<uint16_t> template_provider::wait_until_listening() {
    co_return co_await listening_.async_receive(::asio::use_awaitable);
}

::asio::awaitable<void> template_provider::run_owned() {
    using ::asio::ip::tcp;
    using namespace ::asio::experimental::awaitable_operators;

    std::error_code ec;
    auto const address = ::asio::ip::make_address(settings_.bind, ec);
    if (ec) {
        spdlog::error("[sv2] invalid bind address '{}': {}", settings_.bind, ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    tcp::endpoint const endpoint(address, settings_.port);
    tcp::acceptor acceptor(owner_);
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        spdlog::error("[sv2] failed to open acceptor: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor.bind(endpoint, ec);
    if (ec) {
        spdlog::error("[sv2] failed to bind {}:{}: {}", settings_.bind, settings_.port,
            ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    acceptor.listen(tcp::socket::max_listen_connections, ec);
    if (ec) {
        spdlog::error("[sv2] failed to listen: {}", ec.message());
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }

    auto const bound = acceptor.local_endpoint(ec);
    if (ec) {
        (void)listening_.try_send(ec, uint16_t{});
        co_return;
    }
    (void)listening_.try_send(std::error_code{}, bound.port());
    spdlog::info("[sv2] Template Provider listening on {}:{} (plaintext)", settings_.bind,
        bound.port());

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
            if ( ! failure) failure = std::current_exception();
        }
        for (auto const& socket : connections) {
            socket->close(ignored);
        }
    };
    auto reap = [&]() -> ::asio::awaitable<void> {
        auto error = co_await ended.async_receive(::asio::use_awaitable);
        --pending;
        if (error && ! failure) failure = error;
    };
    auto accept_connections = [&]() -> ::asio::awaitable<void> {
        try {
            while ( ! stopping) {
                if (pending == capacity) {
                    co_await reap();
                    continue;
                }
                tcp::socket socket(owner_);
                auto [aec] = co_await acceptor.async_accept(socket,
                    ::asio::as_tuple(::asio::use_awaitable));
                if (aec) {
                    if (stopping || aec == ::asio::error::operation_aborted) break;
                    spdlog::warn("[sv2] accept failed: {}; retrying", aec.message());
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
                // drain below, even for partial frames or handler errors.
                try {
                    ::asio::co_spawn(owner_, serve_connection(*child),
                        [&, child](std::exception_ptr error) {
                            std::error_code ignored;
                            child->close(ignored);
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
        spdlog::info("[sv2] Stop received; closing listener and {} connections", connections.size());
        close_owned();
    };
    co_await (accept_connections() && receive_stop());
    spdlog::info("[sv2] Listener stopped; all connections joined");
    if (failure) std::rethrow_exception(failure);
}

} // namespace kth::node::sv2
