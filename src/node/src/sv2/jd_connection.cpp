// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/jd_connection.hpp>

#include <array>
#include <chrono>
#include <exception>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/node/detail/scope_action.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/noise.hpp>

namespace kth::node::sv2 {

namespace {

std::optional<uint32_t> unix_time() {
    auto const now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < 0 || now > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    return static_cast<uint32_t>(now);
}

size_t wire_size(data_chunk const& frame) {
    auto const payload = frame.size() - message_header::size;
    auto const chunks = (payload + noise_transport::max_chunk_size - 1)
        / noise_transport::max_chunk_size;
    return noise_transport::encrypted_header_size + payload + 16 * chunks;
}

} // namespace

jd_connection::jd_connection(::asio::any_io_executor executor, std::string host,
    uint16_t port, hash_digest authority, std::chrono::milliseconds timeout)
    : owner_(::asio::make_strand(std::move(executor)))
    , host_(std::move(host))
    , port_(port)
    , authority_(authority)
    , timeout_(timeout)
    , outgoing_(owner_, max_queued_frames)
    , stopping_(owner_, 1)
{}

bool jd_connection::send(data_chunk frames) {
    if ( ! accepting_) return false;
    auto const refuse = [this] {
        accepting_ = false;
        (void)stopping_.try_send(std::error_code{}, 2);
        return false;
    };
    // One entry may hold several frames: BCH large templates (0x4C54) send
    // DeclareMiningJob.Transactions pages ahead of DeclareMiningJob, and a
    // large template's pages must not fill the bounded queue one by one.
    if (frames.empty()) return refuse();
    for (size_t offset = 0; offset < frames.size();) {
        if (frames.size() - offset < message_header::size) return refuse();
        byte_reader source(byte_span(frames).subspan(offset, message_header::size));
        auto const header = message_header::from_data(source);
        if ( ! header || header->msg_length > max_send_payload_size
            || header->msg_length > frames.size() - offset - message_header::size) {
            return refuse();
        }
        offset += message_header::size + header->msg_length;
    }
    if ( ! outgoing_.try_send(std::error_code{}, std::move(frames))) return refuse();
    return true;
}

void jd_connection::stop() {
    accepting_ = false;
    (void)stopping_.try_send(std::error_code{}, 1);
}

::asio::awaitable<std::string> jd_connection::run(
    std::function<bool()> ready, std::function<bool(data_chunk)> received) {
    if (started_.exchange(true)) co_return "upstream connection already started";
    try {
        co_return co_await ::asio::co_spawn(owner_,
            run_owned(std::move(ready), std::move(received)), ::asio::use_awaitable);
    } catch (std::exception const& e) {
        stop();
        co_return std::string("upstream exception: ") + e.what();
    }
}

::asio::awaitable<std::string> jd_connection::run_owned(
    std::function<bool()> ready, std::function<bool(data_chunk)> received) {
    using namespace ::asio::experimental::awaitable_operators;
    using ::asio::ip::tcp;

    tcp::resolver resolver(owner_);
    tcp::socket socket(owner_);
    std::string reason;
    auto close = [&](std::string value) {
        if (reason.empty()) reason = std::move(value);
        accepting_ = false;
        std::error_code ignored;
        resolver.cancel();
        socket.close(ignored);
        outgoing_.cancel();
        outgoing_.close();
        stopping_.cancel();
        stopping_.close();
    };
    detail::scope_action cleanup([&] { close("upstream stopped"); }, true);
    if (host_.empty() || host_.size() > 255 || port_ == 0
        || timeout_.count() <= 0 || timeout_ > std::chrono::minutes(10)) {
        co_return "invalid upstream connection settings";
    }

    std::optional<noise_transport> transport;
    auto exchange = [&]() -> ::asio::awaitable<bool> {
        auto [resolve_error, endpoints] = co_await resolver.async_resolve(host_,
            std::to_string(port_), ::asio::as_tuple(::asio::use_awaitable));
        if (resolve_error) {
            reason = "upstream address resolution failed";
            co_return false;
        }
        auto [connect_error, endpoint] = co_await ::asio::async_connect(socket, endpoints,
            ::asio::as_tuple(::asio::use_awaitable));
        if (connect_error) {
            reason = "upstream connection failed";
            co_return false;
        }

        auto ephemeral = noise_keypair::generate();
        auto [write_error, written] = co_await ::asio::async_write(socket,
            ::asio::buffer(ephemeral.ellswift()), ::asio::as_tuple(::asio::use_awaitable));
        if (write_error) {
            reason = "upstream handshake write failed";
            co_return false;
        }
        std::array<uint8_t, noise_responder::flight_size> response{};
        auto [read_error, size] = co_await ::asio::async_read(socket,
            ::asio::buffer(response), ::asio::as_tuple(::asio::use_awaitable));
        if (read_error) {
            reason = "upstream handshake ended early";
            co_return false;
        }
        // The first flight is only this ephemeral encoding. Constructing the
        // transcript after the response checks validity at authentication time,
        // rather than accepting a certificate that expired during a slow read.
        auto const now = unix_time();
        if ( ! now) {
            reason = "clock outside certificate timestamp range";
            co_return false;
        }
        noise_initiator initiator(std::move(ephemeral), authority_, *now);
        auto const first = initiator.start();
        if ( ! first || ! initiator.finish(response)) {
            reason = "upstream Noise authentication failed";
            co_return false;
        }
        transport.emplace(*initiator.keys(), true, max_payload_size);
        co_return true;
    };
    ::asio::steady_timer deadline(owner_);
    deadline.expires_after(timeout_);
    auto startup_timeout = [&]() -> ::asio::awaitable<void> {
        auto [error] = co_await deadline.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if ( ! error) {
            resolver.cancel();
            std::error_code ignored;
            socket.close(ignored);
        }
    };
    auto startup_stop = [&]() -> ::asio::awaitable<int> {
        auto [error, request] = co_await stopping_.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        if ( ! error) {
            resolver.cancel();
            std::error_code ignored;
            socket.close(ignored);
        }
        co_return request;
    };
    auto const start = co_await (exchange() || startup_timeout() || startup_stop());
    if (start.index() == 1) co_return "upstream connection or handshake timed out";
    if (start.index() == 2) {
        auto const request = std::get<2>(start);
        co_return request == 2 ? "upstream output queue refused a frame" : "upstream stopped";
    }
    if ( ! std::get<0>(start)) co_return reason;
    if ( ! ready()) co_return "upstream setup refused";

    auto read_frames = [&]() -> ::asio::awaitable<void> {
        detail::scope_action finish([&] { close("upstream read ended"); }, true);
        std::array<uint8_t, 8192> encrypted{};
        ::asio::steady_timer assembly(owner_);
        size_t partial = 0;
        for (;;) {
            std::error_code read_error;
            size_t size = 0;
            if (partial == 0) {
                // A healthy upstream may be quiet between jobs. The protocol
                // owner bounds outstanding requests; only a started frame has
                // an assembly deadline here, so silence cannot extend one.
                std::tie(read_error, size) = co_await socket.async_read_some(
                    ::asio::buffer(encrypted), ::asio::as_tuple(::asio::use_awaitable));
                assembly.expires_after(timeout_);
            } else {
                auto const read = co_await (socket.async_read_some(
                    ::asio::buffer(encrypted), ::asio::as_tuple(::asio::use_awaitable))
                    || assembly.async_wait(::asio::as_tuple(::asio::use_awaitable)));
                if (read.index() != 0) {
                    close("upstream frame assembly timed out");
                    co_return;
                }
                std::tie(read_error, size) = std::get<0>(read);
            }
            if (read_error) {
                close("upstream connection dropped");
                co_return;
            }
            partial += size;
            auto frames = transport->feed(byte_span(encrypted).first(size));
            if ( ! frames) {
                close("upstream encrypted frame refused");
                co_return;
            }
            for (auto& frame : *frames) {
                partial -= wire_size(frame);
                if ( ! received(std::move(frame))) {
                    close("upstream protocol response refused");
                    co_return;
                }
            }
            // Any remaining bytes start the frame after the last completed
            // one, and arrived in this read. Partial reads never reset it.
            if ( ! frames->empty() && partial != 0) assembly.expires_after(timeout_);
        }
    };
    auto write_frames = [&]() -> ::asio::awaitable<void> {
        detail::scope_action finish([&] { close("upstream write ended"); }, true);
        for (;;) {
            auto [error, frames] = co_await outgoing_.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));
            if (error) co_return;
            // Noise encrypts each SV2 frame on its own. send() checked that
            // the entry is a whole number of frames.
            std::optional<data_chunk> encrypted = data_chunk{};
            for (size_t offset = 0; encrypted && offset < frames.size();) {
                byte_reader source(byte_span(frames).subspan(offset, message_header::size));
                auto const header = message_header::from_data(source);
                auto const size = message_header::size + size_t{header->msg_length};
                auto const bytes = transport->encrypt_frame(byte_span(frames).subspan(offset, size));
                if (bytes) encrypted->insert(encrypted->end(), bytes->begin(), bytes->end());
                else encrypted.reset();
                offset += size;
            }
            if ( ! encrypted) {
                close("upstream outgoing frame refused");
                co_return;
            }
            ::asio::steady_timer writing(owner_);
            writing.expires_after(timeout_);
            auto const result = co_await (::asio::async_write(socket,
                ::asio::buffer(*encrypted), ::asio::as_tuple(::asio::use_awaitable))
                || writing.async_wait(::asio::as_tuple(::asio::use_awaitable)));
            if (result.index() != 0) {
                close("upstream write timed out");
                co_return;
            }
            auto const [write_error, written] = std::get<0>(result);
            if (write_error) {
                close("upstream write failed");
                co_return;
            }
        }
    };
    auto receive_stop = [&]() -> ::asio::awaitable<void> {
        auto [error, request] = co_await stopping_.async_receive(
            ::asio::as_tuple(::asio::use_awaitable));
        close(request == 2 ? "upstream output queue refused a frame" : "upstream stopped");
    };
    co_await (read_frames() && write_frames() && receive_stop());
    co_return reason;
}

} // namespace kth::node::sv2
