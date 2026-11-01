// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.
#ifndef KTH_NETWORK_BROWSER_STREAM_HPP
#define KTH_NETWORK_BROWSER_STREAM_HPP
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <emscripten/websocket.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace kth::network {
// A WebSocket is a stream of BCH bytes, not a stream of BCH messages. This
// adapter preserves partial reads and divides writes at the bridge frame cap.
// One Worker owns each state. Saturation terminates the session: continuing
// after discarding any stream byte would corrupt BCH framing.
class browser_stream {
    struct state {
        explicit state(::asio::any_io_executor executor) : executor(std::move(executor)) { browser::runtime::current().stream_created(); }
        ::asio::any_io_executor executor;
        ::asio::any_io_executor read_executor;
        ::asio::any_io_executor connect_executor;
        EMSCRIPTEN_WEBSOCKET_T socket = 0;
        std::deque<std::vector<unsigned char>> frames;
        std::size_t offset = 0;
        std::size_t buffered = 0;
        bool stopped = false;
        std::error_code terminal_error = ::asio::error::operation_aborted;
        ::asio::mutable_buffer read_buffer;
        ::asio::any_completion_handler<void(std::error_code, std::size_t)> read;
        ::asio::any_completion_handler<void(std::error_code)> connect;
        ::asio::cancellation_slot read_slot;
        ::asio::cancellation_slot connect_slot;
        static constexpr std::size_t capacity = 1024 * 1024;
        void finish_read(std::error_code ec, std::size_t size) {
            read_slot.clear();
            auto handler = std::move(read);
            ::asio::post(read_executor, [handler=std::move(handler), ec, size]() mutable { handler(ec, size); });
            browser::runtime::current().wake();
        }
        void finish_connect(std::error_code ec) {
            connect_slot.clear();
            auto handler = std::move(connect);
            ::asio::post(connect_executor, [handler=std::move(handler), ec]() mutable { handler(ec); });
            browser::runtime::current().wake();
        }
        void receive() {
            if (!read || frames.empty()) return;
            auto& frame = frames.front();
            auto size = std::min(read_buffer.size(), frame.size() - offset);
            ::asio::buffer_copy(read_buffer, ::asio::buffer(frame.data()+offset, size));
            offset += size; buffered -= size;
            if (offset == frame.size()) { frames.pop_front(); offset = 0; }
            finish_read({}, size);
        }
        void stop(std::error_code ec = ::asio::error::operation_aborted) {
            if (stopped) return;
            stopped = true;
            terminal_error = ec;
            if (connect) finish_connect(ec);
            if (ec == ::asio::error::eof) {
                // WebSocket close follows all message events. Preserve accepted
                // final bytes until reads drain them, then report EOF.
                receive();
            } else {
                frames.clear(); buffered = 0;
            }
            if (read) finish_read(ec, 0);
            if (socket > 0) emscripten_websocket_close(socket, 1000, "session stopped");
        }
        ~state() {
            stop();
            if (socket > 0) emscripten_websocket_delete(socket);
            browser::runtime::current().stream_destroyed();
        }
    };
public:
    using executor_type = ::asio::any_io_executor;
    explicit browser_stream(executor_type executor) : state_(std::make_unique<state>(std::move(executor))) {}
    browser_stream(browser_stream&&) = default;
    browser_stream& operator=(browser_stream&&) = default;
    executor_type get_executor() const { return state_->executor; }
    bool is_open() const { return state_->socket > 0 && !state_->stopped; }
    void cancel(std::error_code& ec) {
        ec.clear();
        auto& s=*state_;
        if (!s.stopped) s.stop();
        else {
            // Explicit cancellation discards unread admission even after a
            // previously received EOF. Already delivered completions stay delivered.
            s.frames.clear(); s.buffered=0; s.offset=0;
            s.terminal_error=::asio::error::operation_aborted;
            if (s.read) s.finish_read(s.terminal_error,0);
        }
    }
    void close(std::error_code& ec) { cancel(ec); }
    void shutdown(::asio::socket_base::shutdown_type, std::error_code& ec) { cancel(ec); }
    template<class CompletionToken>
    auto async_connect(std::string url, CompletionToken&& token) {
        return ::asio::async_initiate<CompletionToken, void(std::error_code)>(
            [this, url=std::move(url)](auto handler) {
                auto& s = *state_;
                assert(!s.connect && s.socket == 0);
                s.connect_slot = ::asio::get_associated_cancellation_slot(handler);
                s.connect_executor = ::asio::get_associated_executor(handler, s.executor);
                s.connect = std::move(handler);
                if (s.connect_slot.is_connected()) s.connect_slot.assign([pointer=&s](::asio::cancellation_type type) {
                    if (type != ::asio::cancellation_type::none) pointer->stop();
                });
                EmscriptenWebSocketCreateAttributes attributes{};
                attributes.url = url.c_str();
                attributes.createOnMainThread = EM_FALSE;
                s.socket = emscripten_websocket_new(&attributes);
                if (s.socket <= 0) { s.stop(::asio::error::connection_refused); return; }
                emscripten_websocket_set_onopen_callback(s.socket, &s, [](int, EmscriptenWebSocketOpenEvent const*, void* pointer) -> EM_BOOL {
                    auto& s = *static_cast<state*>(pointer);
                    if (s.connect) s.finish_connect({});
                    return EM_TRUE;
                });
                emscripten_websocket_set_onmessage_callback(s.socket, &s, [](int, EmscriptenWebSocketMessageEvent const* event, void* pointer) -> EM_BOOL {
                    auto& s = *static_cast<state*>(pointer);
                    if (s.stopped) return EM_TRUE;
                    if (event->isText || event->numBytes > 65536 || event->numBytes > state::capacity - s.buffered || s.frames.size() >= 64) {
                        s.stop(::asio::error::no_buffer_space); return EM_TRUE;
                    }
                    if (event->numBytes) {
                        auto const* data = static_cast<unsigned char const*>(event->data);
                        s.frames.emplace_back(data, data + event->numBytes);
                        s.buffered += event->numBytes;
                        s.receive();
                    }
                    return EM_TRUE;
                });
                emscripten_websocket_set_onerror_callback(s.socket, &s, [](int, EmscriptenWebSocketErrorEvent const*, void* pointer) -> EM_BOOL {
                    static_cast<state*>(pointer)->stop(::asio::error::connection_reset); return EM_TRUE;
                });
                emscripten_websocket_set_onclose_callback(s.socket, &s, [](int, EmscriptenWebSocketCloseEvent const* event, void* pointer) -> EM_BOOL {
                    if (event->code == 1000 || event->code == 1001) static_cast<state*>(pointer)->stop(::asio::error::eof);
                    else static_cast<state*>(pointer)->stop(::asio::error::connection_reset);
                    return EM_TRUE;
                });
            }, token);
    }
    template<class MutableBufferSequence, class CompletionToken>
    auto async_read_some(MutableBufferSequence buffers, CompletionToken&& token) {
        return ::asio::async_initiate<CompletionToken, void(std::error_code, std::size_t)>(
            [this, buffers](auto handler) {
                auto& s = *state_;
                assert(!s.read);
                s.read_slot = ::asio::get_associated_cancellation_slot(handler);
                s.read_executor = ::asio::get_associated_executor(handler, s.executor);
                s.read = std::move(handler);
                if (s.read_slot.is_connected()) s.read_slot.assign([pointer=&s](::asio::cancellation_type type) {
                    if (type != ::asio::cancellation_type::none) pointer->stop();
                });
                s.read_buffer = {};
                for (auto it=::asio::buffer_sequence_begin(buffers); it!=::asio::buffer_sequence_end(buffers); ++it) {
                    if (it->size()) { s.read_buffer = *it; break; }
                }
                if (s.stopped && (s.terminal_error != ::asio::error::eof || s.frames.empty())) s.finish_read(s.terminal_error, 0);
                else if (!s.read_buffer.size()) s.finish_read({}, 0);
                else s.receive();
            }, token);
    }
    template<class ConstBufferSequence, class CompletionToken>
    auto async_write_some(ConstBufferSequence buffers, CompletionToken&& token) {
        return ::asio::async_initiate<CompletionToken, void(std::error_code, std::size_t)>(
            [this, buffers](auto handler) mutable {
                auto& s = *state_;
                std::error_code ec;
                std::size_t size = 0;
                ::asio::const_buffer first;
                for (auto it=::asio::buffer_sequence_begin(buffers); it!=::asio::buffer_sequence_end(buffers); ++it) {
                    if (it->size()) { first = *it; break; }
                }
                if (s.stopped) ec = s.terminal_error;
                else if (first.size()) {
                    unsigned long pending = 0;
                    size = std::min<std::size_t>(first.size(), 65536);
                    if (emscripten_websocket_get_buffered_amount(s.socket, &pending) != EMSCRIPTEN_RESULT_SUCCESS || pending > state::capacity-size) {
                        ec = ::asio::error::no_buffer_space;
                    } else if (emscripten_websocket_send_binary(s.socket, const_cast<void*>(first.data()), size) != EMSCRIPTEN_RESULT_SUCCESS) {
                        ec = ::asio::error::connection_reset;
                    }
                    if (ec) { size = 0; s.stop(ec); }
                }
                // Browser WebSocket has no drain event. Bounded admission either
                // accepts bytes or terminates the session; it never polls bufferedAmount.
                auto completion_executor = ::asio::get_associated_executor(handler, s.executor);
                ::asio::post(completion_executor, [handler=std::move(handler), ec, size]() mutable { handler(ec, size); });
                browser::runtime::current().wake();
            }, token);
    }
private:
    std::unique_ptr<state> state_;
};
}
#endif
