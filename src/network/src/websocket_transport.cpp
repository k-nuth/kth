// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <kth/network/websocket_transport.hpp>
#if defined(KTH_WITH_WEBSOCKET)
#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <deque>
#include <span>
#include <variant>
#include <asio/as_tuple.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <asio/read_until.hpp>
#include <kth/infrastructure/utility/platform_timer.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <wslay/wslay.h>

namespace kth::network {
namespace {
using namespace ::asio::experimental::awaitable_operators;
constexpr size_t chunk_limit = 65536;
constexpr size_t http_limit = 8192;
constexpr size_t queue_limit = 4;
// Every operation runs on the peer's strand: channels without locks.
using write_completion = async_channel<size_t>;
struct write_request {
    data_chunk bytes;
    std::shared_ptr<write_completion> completion;
};
}

struct websocket_transport::impl {
    ::asio::ip::tcp::socket& socket;
    async_channel<data_chunk> decoded;
    async_channel<write_request> writes;
    // Messages the codec decoded in one receive turn, handed to `decoded` before
    // the next socket read: a reader that falls behind stops reading from the
    // socket, it does not end the session.
    std::deque<data_chunk> received;
    wslay_event_context_ptr codec{};
    data_chunk input;
    size_t input_offset{};
    data_chunk output;
    data_chunk current;
    size_t current_offset{};
    std::optional<::asio::ip::address> forwarded;
    bool stopped{};
    bool clean{};
    bool refused{};
    bool running{};

    explicit impl(::asio::ip::tcp::socket& s)
        : socket(s), decoded(s.get_executor(), queue_limit), writes(s.get_executor(), queue_limit) {}
    ~impl() { if (codec) wslay_event_context_free(codec); }

    static ssize_t recv(wslay_event_context_ptr context, uint8_t* dest, size_t size, int, void* user) {
        auto& self = *static_cast<impl*>(user);
        auto const available = self.input.size() - self.input_offset;
        if (!available) { wslay_event_set_error(context, WSLAY_ERR_WOULDBLOCK); return -1; }
        auto const count = std::min(size, available);
        std::memcpy(dest, self.input.data() + self.input_offset, count);
        self.input_offset += count;
        return static_cast<ssize_t>(count);
    }
    static ssize_t send(wslay_event_context_ptr context, uint8_t const* data, size_t size, int, void* user) {
        auto& self = *static_cast<impl*>(user);
        auto const available = chunk_limit + 14 - self.output.size();
        if (!available) { wslay_event_set_error(context, WSLAY_ERR_WOULDBLOCK); return -1; }
        auto const count = std::min(size, available);
        self.output.insert(self.output.end(), data, data + count);
        return static_cast<ssize_t>(count);
    }
    static void message(wslay_event_context_ptr context, wslay_event_on_msg_recv_arg const* arg, void* user) {
        auto& self = *static_cast<impl*>(user);
        if (arg->opcode == WSLAY_BINARY_FRAME) {
            if (arg->msg_length) self.received.emplace_back(arg->msg, arg->msg + arg->msg_length);
        } else if (arg->opcode == WSLAY_TEXT_FRAME) {
            self.refused = true;
            wslay_event_queue_close(context, WSLAY_CODE_UNSUPPORTED_DATA, nullptr, 0);
        } else if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
            self.clean = arg->status_code == WSLAY_CODE_NORMAL_CLOSURE || arg->status_code == WSLAY_CODE_GOING_AWAY || arg->status_code == WSLAY_CODE_NO_STATUS_RCVD;
        }
    }
    void stop() {
        if (stopped) return;
        stopped = true;
        std::error_code ignored;
        socket.cancel(ignored);
        socket.shutdown(::asio::ip::tcp::socket::shutdown_both, ignored);
        decoded.cancel(); decoded.close();
        writes.cancel(); writes.close();
    }
    ::asio::awaitable<std::error_code> flush() {
        while (wslay_event_want_write(codec)) {
            output.clear();
            if (wslay_event_send(codec) < 0) co_return ::asio::error::fault;
            if (output.empty()) break;
            auto [ec, count] = co_await ::asio::async_write(socket, ::asio::buffer(output), ::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return ec;
        }
        co_return std::error_code{};
    }
    ::asio::awaitable<void> reader() {
        std::array<uint8_t, 16384> buffer;
        while (!stopped) {
            if (input_offset == input.size()) {
                auto [ec, n] = co_await socket.async_read_some(::asio::buffer(buffer), ::asio::as_tuple(::asio::use_awaitable));
                if (ec) break;
                input.assign(buffer.begin(), buffer.begin() + n);
                input_offset = 0;
            }
            if (wslay_event_recv(codec) < 0) break;
            // A stalled wire writer must not let automatic pong/close queues
            // grow without bound. One recv turn admits at most 16 KiB of wire
            // input; excessive control traffic ends the session, never polls.
            if (wslay_event_get_queued_msg_count(codec) > 32 ||
                wslay_event_get_queued_msg_length(codec) > chunk_limit * 2) {
                refused = true;
                wslay_event_queue_close(codec, 1013, nullptr, 0);
            }
            // Control replies (a pong, a close) this turn queued go out now, not
            // after the reader has taken the data: wake the writer before
            // waiting below. A queued data write also wakes it; if the queue is
            // full, there is already an event that will flush them.
            if (wslay_event_want_write(codec)) writes.try_send(std::error_code{}, write_request{});
            // Backpressure: what this turn decoded (at most one 64 KiB message
            // and 16 KiB of input) waits for room before the socket is read again.
            bool handed = true;
            while (!received.empty()) {
                auto [ec] = co_await decoded.async_send(std::error_code{}, std::move(received.front()),
                    ::asio::as_tuple(::asio::use_awaitable));
                received.pop_front();
                if (ec) { handed = false; break; }
            }
            if (!handed) break;
            if (!wslay_event_want_read(codec) || refused) {
                // Writer owns all wire writes. Wake it and wait for the close
                // reply; run() bounds this close via its outer write deadline.
                if (wslay_event_want_write(codec)) {
                    auto done = std::make_shared<write_completion>(socket.get_executor(), 1);
                    auto [ec] = co_await writes.async_send(std::error_code{}, write_request{{}, done}, ::asio::as_tuple(::asio::use_awaitable));
                    if (!ec) {
                        auto [send_ec, ignored] = co_await done->async_receive(::asio::as_tuple(::asio::use_awaitable));
                        (void)send_ec; (void)ignored;
                    }
                }
                break;
            }
        }
        stop();
    }
    ::asio::awaitable<void> writer() {
        while (!stopped) {
            auto [ec, request] = co_await writes.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            std::error_code queue_ec;
            if (!request.bytes.empty()) {
                wslay_event_msg message{WSLAY_BINARY_FRAME, request.bytes.data(), request.bytes.size()};
                // A peer close may have been parsed while this request waited.
                // Flush its queued close reply even though data is no longer
                // admitted, rather than dropping the reply on queue failure.
                if (wslay_event_queue_msg(codec, &message)) queue_ec = ::asio::error::operation_aborted;
            }
            kth::steady_timer deadline(socket.get_executor());
            deadline.expires_after(std::chrono::seconds(10));
            auto result = co_await (flush() || deadline.async_wait(::asio::as_tuple(::asio::use_awaitable)));
            auto send_ec = result.index() == 0 ? std::get<0>(result) : std::error_code(::asio::error::timed_out);
            if (request.completion) request.completion->try_send(send_ec ? send_ec : queue_ec, request.bytes.size());
            if (send_ec || queue_ec) break;
        }
        stop();
    }
};

namespace {
// Sec-WebSocket-Key: 16 bytes in canonical base64 (RFC 6455, section 4.1).
bool valid_key(std::string const& text) {
    auto const key = decode_base64(text);
    return key && key->size() == 16 && encode_base64(*key) == text;
}
}

websocket_transport::websocket_transport(::asio::ip::tcp::socket& socket) : impl_(std::make_unique<impl>(socket)) {}
websocket_transport::~websocket_transport() = default;

::asio::awaitable<std::error_code> websocket_transport::accept(websocket_server_options const& options) {
    auto accept_http = [&]() -> ::asio::awaitable<std::error_code> {
        auto& self = *impl_;
        std::string input;
        auto [ec, count] = co_await ::asio::async_read_until(self.socket, ::asio::dynamic_buffer(input, http_limit), "\r\n\r\n", ::asio::as_tuple(::asio::use_awaitable));
        if (ec) co_return ec;
        boost::beast::http::request_parser<boost::beast::http::empty_body> parser;
        parser.header_limit(http_limit);
        parser.body_limit(0);
        boost::system::error_code parse_ec;
        parser.put(boost::asio::buffer(input.data(), count), parse_ec);
        auto reject = [&]() -> ::asio::awaitable<std::error_code> {
            std::string response = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            auto [write_ec, n] = co_await ::asio::async_write(self.socket, ::asio::buffer(response), ::asio::as_tuple(::asio::use_awaitable));
            (void)write_ec; (void)n;
            co_return ::asio::error::access_denied;
        };
        if (parse_ec || !parser.is_done()) co_return co_await reject();
        auto const& request = parser.get();
        auto get = [&](boost::beast::http::field field) { return std::string(request[field]); };
        auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        auto connection = lower(get(boost::beast::http::field::connection));
        bool connection_upgrade = false;
        for (size_t start = 0; start < connection.size();) {
            auto end = connection.find(',', start);
            if (end == std::string::npos) end = connection.size();
            auto token = connection.substr(start, end-start);
            auto first = token.find_first_not_of(" \t");
            auto last = token.find_last_not_of(" \t");
            if (first != std::string::npos && token.substr(first, last-first+1) == "upgrade") connection_upgrade = true;
            start = end + 1;
        }
        auto const key_text = get(boost::beast::http::field::sec_websocket_key);
        auto const origin = get(boost::beast::http::field::origin);
        if (request.method() != boost::beast::http::verb::get || request.version() != 11 || request.target() != options.path ||
            !connection_upgrade || lower(get(boost::beast::http::field::upgrade)) != "websocket" ||
            get(boost::beast::http::field::sec_websocket_version) != "13" ||
            request.count(boost::beast::http::field::host) != 1 || get(boost::beast::http::field::host).empty() ||
            request.count(boost::beast::http::field::connection) != 1 || request.count(boost::beast::http::field::upgrade) != 1 ||
            request.count(boost::beast::http::field::sec_websocket_version) != 1 ||
            request.count(boost::beast::http::field::sec_websocket_key) != 1 || request.count(boost::beast::http::field::origin) != 1 ||
            !valid_key(key_text) ||
            std::find(options.origins.begin(), options.origins.end(), origin) == options.origins.end()) co_return co_await reject();
        // Behind a trusted proxy every client shares its address: the client's
        // own is the X-Forwarded-For entry that proxy appended (the last one),
        // and a connection from it without one is refused.
        std::error_code remote_ec;
        auto const remote = self.socket.remote_endpoint(remote_ec).address();
        if (!remote_ec && std::find(options.trusted_proxies.begin(), options.trusted_proxies.end(), remote) !=
                options.trusted_proxies.end()) {
            if (request.count(boost::beast::http::field::x_forwarded_for) != 1) co_return co_await reject();
            auto const chain = get(boost::beast::http::field::x_forwarded_for);
            auto last = chain.substr(chain.rfind(',') == std::string::npos ? 0 : chain.rfind(',') + 1);
            auto const first = last.find_first_not_of(" \t");
            auto const end = last.find_last_not_of(" \t");
            last = first == std::string::npos ? std::string{} : last.substr(first, end - first + 1);
            std::error_code client_ec;
            auto const client = ::asio::ip::make_address(last, client_ec);
            if (client_ec) co_return co_await reject();
            self.forwarded = client;
        }
        auto text = key_text + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        auto hash = sha1_hash(byte_span(reinterpret_cast<uint8_t const*>(text.data()), text.size()));
        auto accepted = encode_base64(hash);
        std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accepted + "\r\n\r\n";
        auto [write_ec, n] = co_await ::asio::async_write(self.socket, ::asio::buffer(response), ::asio::as_tuple(::asio::use_awaitable));
        (void)n;
        if (write_ec) co_return write_ec;
        self.input.assign(input.begin() + count, input.end());
        wslay_event_callbacks callbacks{};
        callbacks.recv_callback = impl::recv;
        callbacks.send_callback = impl::send;
        callbacks.on_msg_recv_callback = impl::message;
        if (wslay_event_context_server_init(&self.codec, &callbacks, &self)) co_return ::asio::error::no_memory;
        wslay_event_config_set_max_recv_msg_length(self.codec, chunk_limit);
        co_return std::error_code{};
    };
    kth::steady_timer deadline(impl_->socket.get_executor());
    deadline.expires_after(options.handshake_timeout);
    auto result = co_await (accept_http() || deadline.async_wait(::asio::as_tuple(::asio::use_awaitable)));
    co_return result.index() == 0 ? std::get<0>(result) : std::error_code(::asio::error::timed_out);
}
::asio::awaitable<void> websocket_transport::run() {
    impl_->running = true;
    co_await (impl_->reader() || impl_->writer());
    impl_->stop();
}
::asio::awaitable<websocket_transport::result> websocket_transport::read_exact(::asio::mutable_buffer buffer) {
    auto* out = static_cast<uint8_t*>(buffer.data());
    size_t done = 0;
    while (done < buffer.size()) {
        if (impl_->current_offset == impl_->current.size()) {
            auto [ec, bytes] = co_await impl_->decoded.async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return result{ec, done};
            impl_->current = std::move(bytes);
            impl_->current_offset = 0;
        }
        auto count = std::min(buffer.size()-done, impl_->current.size()-impl_->current_offset);
        std::memcpy(out+done, impl_->current.data()+impl_->current_offset, count);
        impl_->current_offset += count;
        done += count;
    }
    co_return result{{}, done};
}
::asio::awaitable<websocket_transport::result> websocket_transport::write_exact(::asio::const_buffer buffer) {
    auto* bytes = static_cast<uint8_t const*>(buffer.data());
    size_t done = 0;
    while (done < buffer.size()) {
        auto count = std::min(chunk_limit, buffer.size()-done);
        auto complete = std::make_shared<write_completion>(impl_->socket.get_executor(), 1);
        auto [ec] = co_await impl_->writes.async_send(std::error_code{}, write_request{data_chunk(bytes+done, bytes+done+count), complete}, ::asio::as_tuple(::asio::use_awaitable));
        if (ec) co_return result{ec, done};
        auto [write_ec, written] = co_await complete->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (write_ec) co_return result{write_ec, done};
        done += written;
    }
    co_return result{{}, done};
}
void websocket_transport::stop() { impl_->stop(); }
bool websocket_transport::clean_close() const { return impl_->clean; }
std::optional<::asio::ip::address> websocket_transport::forwarded_client() const { return impl_->forwarded; }
}
#endif
