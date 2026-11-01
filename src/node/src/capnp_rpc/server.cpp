// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The asio owner of the Cap'n Proto RPC backend. One strand owns the KJ thread
// handle, the table of running operations and every call into the reader. The
// KJ thread only talks to it through bridge.hpp.

#include <kth/node/capnp_rpc/server.hpp>

#include <cerrno>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/ip/address.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include "capnp_rpc/bridge.hpp"

namespace kth::node::capnp_rpc {

using namespace detail;

namespace {

std::string random_token() {
    std::random_device random;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 32; ++i) {
        auto const byte = static_cast<unsigned>(random()) & 0xffu;
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0xf]);
    }
    return token;
}

// Writes `secret` to `target` so that it is never readable by others and never
// reaches the target through a symlink: a private temporary file is created
// exclusively next to it (O_EXCL|O_NOFOLLOW, mode 0600 at creation, so there is no
// window with wider permissions), written, flushed and closed with every step
// checked, then renamed over the target. rename() replaces a symlink at the
// target instead of following it. The temporary file is removed on any failure.
// On Windows the file inherits the directory's ACL (no portable way to restrict
// it here), the rest is the same.
std::expected<void, std::string> write_private_file(std::filesystem::path const& target,
    std::string const& secret) {
    namespace fs = std::filesystem;
    auto directory = target.parent_path();
    if (directory.empty()) directory = ".";

    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto temporary = directory / (target.filename().string() + ".tmp." + std::to_string(random()));
        std::string const error_prefix = "cannot write " + target.string() + ": ";
#if defined(_WIN32)
        int fd = -1;
        auto const open_error = ::_wsopen_s(&fd, temporary.c_str(),
            _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _SH_DENYRW, _S_IREAD | _S_IWRITE);
        if (open_error == EEXIST) continue;
        if (open_error != 0) {
            return std::unexpected(error_prefix + std::generic_category().message(open_error));
        }
        auto close_fd = [&] { return ::_close(fd); };
        auto write_fd = [&](char const* data, std::size_t size) { return ::_write(fd, data, static_cast<unsigned>(size)); };
        auto sync_fd = [&] { return ::_commit(fd); };
#else
        int const fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return std::unexpected(error_prefix + std::generic_category().message(errno));
        }
        auto close_fd = [&] { return ::close(fd); };
        auto write_fd = [&](char const* data, std::size_t size) { return ::write(fd, data, size); };
        auto sync_fd = [&] { return ::fsync(fd); };
        // The umask can only remove bits from 0600, never add; this is explicit.
        if (::fchmod(fd, 0600) != 0) {
            auto const code = errno;
            close_fd();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected(error_prefix + std::generic_category().message(code));
        }
#endif
        int code = 0;
        std::size_t done = 0;
        while (done < secret.size()) {
            auto const written = write_fd(secret.data() + done, secret.size() - done);
            if (written < 0) {
                if (errno == EINTR) continue;
                code = errno;
                break;
            }
            done += static_cast<std::size_t>(written);
        }
        if (code == 0 && sync_fd() != 0) code = errno;
        if (close_fd() != 0 && code == 0) code = errno;

        std::error_code rename_error;
        if (code == 0) {
            fs::rename(temporary, target, rename_error);
        }
        if (code != 0 || rename_error) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return std::unexpected(error_prefix + (code != 0 ? std::generic_category().message(code)
                                                              : rename_error.message()));
        }
        return {};
    }
    return std::unexpected("cannot write " + target.string() + ": no free temporary name");
}

// Same credential source as the JSON-RPC backend: configured user/password, or
// a generated bitcoind-style cookie. Failing to persist the cookie is an error:
// a listener whose generated secret nobody can read is useless, and one whose
// secret leaked would be worse.
std::expected<credentials, std::string> make_credentials(rpc_settings const& settings,
    std::filesystem::path const& cookie_path) {
    if ( ! settings.user.empty() || ! settings.password.empty()) {
        return credentials{settings.user + ":" + settings.password};
    }
    credentials creds{"__cookie__:" + random_token()};
    auto written = write_private_file(cookie_path, creds.expected);
    if ( ! written) {
        return std::unexpected(written.error());
    }
    spdlog::info("[capnp-rpc] using generated auth cookie ({}); log in as user "
        "'__cookie__' with the cookie's token as password", cookie_path.string());
    return creds;
}

template <typename T>
reply to_reply(result<T>&& value) {
    if (value) {
        return reply{std::in_place_type<T>, std::move(*value)};
    }
    return reply{std::move(value.error())};
}

} // namespace

struct server::impl {
    impl(rpc_settings const& settings, reader& handlers, ::asio::any_io_executor executor,
        server_options options)
        : settings(settings)
        , handlers(handlers)
        , options(std::move(options))
        , owner(::asio::make_strand(std::move(executor)))
        , shared(owner, this->options.limits)
        {}

    // The KJ thread can only end once told to; this keeps destruction from
    // terminating or hanging when run() never got to join it.
    ~impl() {
        shared.request_stop();
        if (kj_thread.joinable()) kj_thread.join();
    }

    // A running operation. Touched on the owner strand only.
    struct entry {
        ::asio::cancellation_signal signal;
        bool cancelled = false;
    };

    ::asio::awaitable<void> run_owned();
    ::asio::awaitable<void> run_operation(entry* running, request_msg request);
    void handle(command&& message);
    void finish(std::uint64_t id, std::exception_ptr error);
    void begin_shutdown();

    rpc_settings const& settings;
    reader& handlers;
    server_options options;
    ::asio::strand<::asio::any_io_executor> owner;
    bridge shared;
    credentials creds;   // set by run_owned, before the transport thread starts

    std::atomic<bool> stop_requested{false};
    std::atomic<bool> started{false};

    // Owner-strand state.
    std::thread kj_thread;
    std::unordered_map<std::uint64_t, std::unique_ptr<entry>> active;
    bool stopping = false;
};

void server::impl::begin_shutdown() {
    if (stopping) return;
    stopping = true;
    shared.request_stop();
    // Cancel each operation at its owner. Whatever cannot be interrupted ends on
    // its own and is joined in run_owned.
    for (auto& [id, running] : active) {
        running->cancelled = true;
        running->signal.emit(::asio::cancellation_type::all);
    }
}

::asio::awaitable<void> server::impl::run_operation(entry* running, request_msg request) {
    reply answer{failure{status::unavailable, "cancelled"}};
    // Cancelled before it started: never call the reader.
    if ( ! running->cancelled) {
        try {
            switch (request.kind) {
                case op_kind::chain_info:
                    answer = to_reply(co_await handlers.get_chain_info());
                    break;
                case op_kind::block:
                    answer = to_reply(co_await handlers.get_block(request.hash));
                    break;
                case op_kind::transaction:
                    answer = to_reply(co_await handlers.get_transaction(request.hash));
                    break;
            }
        } catch (std::exception const& error) {
            // Asio operations report cancellation with their own codes
            // (operation_aborted, channel_cancelled, ...); an operation that was
            // cancelled and then failed is a cancelled one.
            if (running->cancelled) {
                answer = failure{status::unavailable, "cancelled"};
            } else {
                spdlog::error("[capnp-rpc] handler failed: {}", error.what());
                answer = failure{status::internal, "internal error"};
            }
        }
    }

    // The payload bound is enforced here so an oversized buffer is freed on the
    // strand instead of being copied into an outgoing message.
    auto const limit = options.limits.max_payload_bytes;
    auto const size = std::visit([](auto const& value) -> std::size_t {
        using type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<type, block_data> || std::is_same_v<type, transaction_data>) {
            return value.wire.size();
        } else {
            return 0;
        }
    }, answer);
    if (size > limit) {
        answer = failure{status::resource_exhausted,
            "result of " + std::to_string(size) + " bytes exceeds the " +
            std::to_string(limit) + " byte response limit"};
    }

    // No-op if the call (or the whole KJ loop) is already gone.
    request.done->fulfill(std::move(answer));
    co_return;
}

void server::impl::finish(std::uint64_t id, std::exception_ptr error) {
    active.erase(id);
    shared.release();
    if (error) {
        try {
            std::rethrow_exception(error);
        } catch (std::exception const& e) {
            spdlog::error("[capnp-rpc] operation {} ended with error: {}", id, e.what());
        } catch (...) {
            spdlog::error("[capnp-rpc] operation {} ended with an unknown error", id);
        }
    }
    // Let the run loop re-check its exit condition. If the mailbox is full the
    // loop is going to wake anyway.
    if (stopping) (void)shared.post(wake_msg{});
}

void server::impl::handle(command&& message) {
    std::visit([this](auto&& value) {
        using type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<type, request_msg>) {
            if (stopping) {
                // Admitted just before shutdown: answer, never start.
                value.done->fulfill(reply{failure{status::unavailable, "node is stopping"}});
                shared.release();
                return;
            }
            auto const id = value.id;
            auto running = std::make_unique<entry>();
            auto* raw = running.get();
            active.emplace(id, std::move(running));
            ::asio::co_spawn(owner, run_operation(raw, std::move(value)),
                ::asio::bind_cancellation_slot(raw->signal.slot(),
                    [this, id](std::exception_ptr error) { finish(id, error); }));
        } else if constexpr (std::is_same_v<type, cancel_msg>) {
            if (auto found = active.find(value.id); found != active.end()) {
                found->second->cancelled = true;
                found->second->signal.emit(::asio::cancellation_type::all);
            }
        } else if constexpr (std::is_same_v<type, listening_msg>) {
            spdlog::info("[capnp-rpc] Cap'n Proto RPC listening on {}:{}", settings.bind, value.port);
            if (options.on_listening) options.on_listening(value.port);
        }
        // wake_msg, stop_msg, exited_msg: the loop re-checks its flags.
    }, std::move(message));
}

::asio::awaitable<void> server::impl::run_owned() {
    if (started.exchange(true)) {
        spdlog::error("[capnp-rpc] run() called twice");
        co_return;
    }
    // Stopped before it began: start nothing, bind nothing.
    if (stop_requested.load()) co_return;

    std::error_code ec;
    auto const address = ::asio::ip::make_address(settings.bind, ec);
    if (ec) {
        spdlog::error("[capnp-rpc] invalid bind address '{}': {}", settings.bind, ec.message());
        co_return;
    }
    // Credentials cross the socket unencrypted (no TLS in this backend), so
    // remote clients must tunnel; refuse rather than expose logins on the network.
    if ( ! address.is_loopback()) {
        spdlog::error("[capnp-rpc] refusing to bind '{}': this backend serves loopback only "
            "(tunnel remote clients over SSH or TLS)", settings.bind);
        co_return;
    }

    auto made = make_credentials(settings, options.cookie_path);
    if ( ! made) {
        spdlog::error("[capnp-rpc] not listening: {}", made.error());
        co_return;
    }
    creds = std::move(*made);

    try {
        kj_thread = std::thread(run_kj_thread, std::ref(shared), creds, settings.bind, settings.port);
    } catch (std::system_error const& error) {
        spdlog::error("[capnp-rpc] cannot start transport thread: {}", error.what());
        co_return;
    }

    std::exception_ptr failure_ptr;
    bool reported = false;
    auto const done = [&] {
        return stopping && active.empty() && shared.kj_exited.load(std::memory_order_acquire);
    };
    auto const check = [&] {
        if (stop_requested.load()) begin_shutdown();
        if (shared.kj_exited.load(std::memory_order_acquire)) {
            // Failure to bind or accept, or a clean exit after stop: either way
            // there is no transport left to serve.
            if ( ! reported && ! shared.kj_error.empty()) {
                reported = true;
                spdlog::error("[capnp-rpc] transport stopped: {}", shared.kj_error);
            }
            begin_shutdown();
        }
    };

    try {
        for (;;) {
            check();
            if (done()) break;
            auto message = co_await shared.mailbox.async_receive(::asio::use_awaitable);
            handle(std::move(message));
        }
    } catch (...) {
        failure_ptr = std::current_exception();
    }

    // Past this point nothing may abandon the join: cancellation of this
    // coroutine is ignored and every wait below is for work that is finishing.
    begin_shutdown();
    if ( ! done()) {
        co_await ::asio::this_coro::reset_cancellation_state(::asio::disable_cancellation());
        for (;;) {
            check();
            if (done()) break;
            auto message = co_await shared.mailbox.async_receive(::asio::use_awaitable);
            handle(std::move(message));
        }
    }

    // The KJ thread has destroyed every KJ object and is returning; this wait is
    // bounded by thread teardown only.
    if (kj_thread.joinable()) kj_thread.join();
    spdlog::info("[capnp-rpc] stopped; operations and transport thread joined");
    if (failure_ptr) std::rethrow_exception(failure_ptr);
}

server::server(rpc_settings const& settings, reader& handlers,
    ::asio::any_io_executor executor, server_options options)
    : impl_(std::make_unique<impl>(settings, handlers, std::move(executor), std::move(options))) {}

server::~server() = default;

::asio::awaitable<void> server::run() {
    co_await ::asio::co_spawn(impl_->owner, impl_->run_owned(), ::asio::use_awaitable);
}

void server::stop() {
    impl_->stop_requested.store(true);
    // A full mailbox means the owner is about to wake and read the flag anyway.
    (void)impl_->shared.post(stop_msg{});
}

} // namespace kth::node::capnp_rpc
