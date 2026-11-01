// Worker-only executor/transport feasibility control. Not a BCH implementation.
#include <asio.hpp>
#include <asio/experimental/channel.hpp>
#include <emscripten.h>
#include <emscripten/websocket.h>
#include <array>
#include <cstdio>
#include <string>

asio::io_context context;
using channel = asio::experimental::channel<void(std::error_code, std::string)>;
channel incoming(context, 2);
channel deadline(context, 1);
channel real_deadline(context, 1);
EMSCRIPTEN_WEBSOCKET_T socket_id;
long timeout_id;
bool closed = false;
unsigned admitted = 0;
unsigned joined = 0;

// Every external event enters on this Worker. A continuation is scheduled only
// after consuming a full runnable-work budget; there is no idle polling loop.
void drain(void* = nullptr) {
    context.restart();
    unsigned count = 0;
    while (count != 32 && context.poll_one()) ++count;
    if (count == 32) emscripten_async_call(drain, nullptr, 0);
}
void stop() {
    if (closed) return;
    closed = true;
    emscripten_clear_timeout(timeout_id);
    incoming.close();
    deadline.close();
    emscripten_websocket_close(socket_id, 1000, "bounded admission");
}
asio::awaitable<void> reader() {
    auto [timer_ec, ignored] = co_await deadline.async_receive(asio::as_tuple(asio::use_awaitable));
    unsigned bytes = 0;
    for (;;) {
        auto [ec, frame] = co_await incoming.async_receive(asio::as_tuple(asio::use_awaitable));
        if (ec) break;
        bytes += frame.size();
    }
    std::printf("reader joined timer_cancelled=%d admitted=%u bytes=%u\n", bool(timer_ec), admitted, bytes);
    ++joined;
}
EM_BOOL message(int, EmscriptenWebSocketMessageEvent const* event, void*) {
    if (closed) return EM_TRUE;
    if (!event->isText && event->numBytes <= 65536 &&
        incoming.try_send(std::error_code{}, std::string((char*)event->data, event->numBytes))) {
        ++admitted;
    } else {
        std::puts("admission refused: session closes, no dropped live-stream bytes");
        stop();
    }
    drain();
    return EM_TRUE;
}
EM_BOOL opened(int, EmscriptenWebSocketOpenEvent const*, void*) {
    std::puts("websocket open on Worker");
    std::array<unsigned char, 3> payload{1,2,3};
    for (unsigned i=0; i!=3; ++i)
        emscripten_websocket_send_binary(socket_id, payload.data(), payload.size());
    drain();
    return EM_TRUE;
}
EM_BOOL ended(int, EmscriptenWebSocketCloseEvent const*, void*) {
    stop(); drain();
    std::printf("shutdown joined=%u\n", joined);
    emscripten_websocket_delete(socket_id);
    return EM_TRUE;
}
asio::awaitable<void> deadline_control() {
    co_await real_deadline.async_receive(asio::use_awaitable);
    std::puts("real browser deadline resumed coroutine");
}
int main() {
    asio::co_spawn(context, deadline_control(), [](std::exception_ptr) {});
    emscripten_set_timeout([](void*) {
        real_deadline.try_send(std::error_code{}, std::string{}); drain();
    }, 25, nullptr);
    asio::co_spawn(context, reader(), [](std::exception_ptr error) {
        if (error) std::puts("COROUTINE FAILED");
    });
    // A real browser deadline; saturation must cancel this before it fires.
    timeout_id = emscripten_set_timeout([](void*) {
        std::puts("UNEXPECTED deadline fired");
        deadline.try_send(std::error_code{}, std::string{}); drain();
    }, 200, nullptr);
    EmscriptenWebSocketCreateAttributes attributes{};
    attributes.url = "ws://127.0.0.1:18765";
    attributes.createOnMainThread = EM_FALSE;
    socket_id = emscripten_websocket_new(&attributes);
    emscripten_websocket_set_onopen_callback(socket_id, nullptr, opened);
    emscripten_websocket_set_onmessage_callback(socket_id, nullptr, message);
    emscripten_websocket_set_onclose_callback(socket_id, nullptr, ended);
    drain();
    std::puts("main returned; browser event loop remains available");
}
