// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sam.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::network {
namespace {

using socket_type = ::asio::ip::tcp::socket;
using authority = infrastructure::config::authority;
using fields = std::unordered_map<std::string, std::string>;
constexpr size_t max_line_size = 65536;

struct sam_reply_error : std::system_error {
    bool const session_lost;
    explicit sam_reply_error(std::string const& result)
        : std::system_error(error::operation_failed)
        , session_lost(result == "INVALID_ID" || result == "I2P_ERROR")
    {}
};

[[noreturn]] void invalid_reply() {
    throw std::system_error(error::operation_failed);
}

code sam_error_code(std::system_error const& e) {
    if (e.code().category() == error::make_error_code(error::success).category()) return e.code();
    return error::boost_to_error_code(e.code());
}

data_chunk decode_destination(std::string const& text) {
    auto base64 = text;
    for (auto& c : base64) {
        if (c == '-') c = '+';
        else if (c == '~') c = '/';
        else if ( ! ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '=')) invalid_reply();
    }
    data_chunk bytes;
    if ( ! decode_base64(bytes, base64) || encode_base64(bytes) != base64) invalid_reply();
    return bytes;
}

size_t destination_size(data_chunk const& bytes) {
    if (bytes.size() < 387) invalid_reply();
    auto const size = 387 + (size_t(bytes[385]) << 8) + bytes[386];
    if (size > bytes.size()) invalid_reply();
    return size;
}

authority destination_address(data_chunk const& bytes, bool private_key) {
    auto const size = destination_size(bytes);
    if ((private_key && size == bytes.size()) || ( ! private_key && size != bytes.size())) invalid_reply();
    auto const hash = sha256_hash(byte_span(bytes.data(), size));
    return authority(infrastructure::message::network_address(0, 0,
        infrastructure::message::address_network::i2p, hash, 0));
}

std::string const& field(fields const& reply, std::string const& key) {
    auto const pos = reply.find(key);
    if (pos == reply.end() || pos->second.empty()) invalid_reply();
    return pos->second;
}

fields parse_reply(std::string const& line, std::string_view prefix, bool result = true) {
    if ( ! line.starts_with(prefix) || line.size() <= prefix.size() || line[prefix.size()] != ' ') invalid_reply();
    fields values;
    size_t pos = prefix.size() + 1;
    while (pos < line.size()) {
        auto const equal = line.find('=', pos);
        auto const space = line.find(' ', pos);
        if (equal == std::string::npos || equal == pos || (space != std::string::npos && space < equal)) invalid_reply();
        auto const key = line.substr(pos, equal - pos);
        pos = equal + 1;
        std::string value;
        if (pos < line.size() && line[pos] == '"') {
            auto const end = line.find('"', ++pos);
            if (end == std::string::npos) invalid_reply();
            value = line.substr(pos, end - pos);
            pos = end + 1;
        } else {
            auto const end = line.find(' ', pos);
            value = line.substr(pos, end == std::string::npos ? end : end - pos);
            pos = end == std::string::npos ? line.size() : end;
        }
        if ( ! values.emplace(key, std::move(value)).second) invalid_reply();
        if (pos < line.size() && line[pos++] != ' ') invalid_reply();
    }
    if (result || values.contains("RESULT")) {
        auto const& status = field(values, "RESULT");
        if (status != "OK") throw sam_reply_error(status);
    }
    return values;
}

::asio::awaitable<std::string> read_line(socket_type& socket) {
    std::string line;
    for (size_t i = 0; i < max_line_size; ++i) {
        char c;
        // Reading exactly one byte leaves any following BCH message on the stream.
        co_await ::asio::async_read(socket, ::asio::buffer(&c, 1), ::asio::use_awaitable);
        if (c == '\n') co_return line;
        if (c < 0x20 || c > 0x7e) invalid_reply();
        line.push_back(c);
    }
    invalid_reply();
}

::asio::awaitable<fields> request(socket_type& socket, std::string command,
    std::string_view prefix, bool result = true)
{
    command.push_back('\n');
    co_await ::asio::async_write(socket, ::asio::buffer(command), ::asio::use_awaitable);
    co_return parse_reply(co_await read_line(socket), prefix, result);
}

template <typename T>
awaitable_expected<T> with_deadline(awaitable_expected<T> operation,
    std::chrono::steady_clock::duration timeout)
{
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    timer.expires_after(timeout);
    using namespace ::asio::experimental::awaitable_operators;
    auto result = co_await (std::move(operation) || timer.async_wait(::asio::as_tuple(::asio::use_awaitable)));
    if (result.index() == 1) co_return std::unexpected(error::channel_timeout);
    co_return std::move(std::get<0>(result));
}

std::optional<std::string> read_key(kth::path const& file) {
    std::error_code ec;
    auto const status = std::filesystem::symlink_status(file, ec);
    if (status.type() == std::filesystem::file_type::not_found) return std::nullopt;
    if (ec || ! std::filesystem::is_regular_file(status)) throw std::system_error(error::file_system);
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) throw std::system_error(error::file_system);
    std::ifstream input(file, std::ios::binary);
    std::string key;
    char c;
    while (key.size() < max_line_size && input.get(c)) key.push_back(c);
    if ( ! input.eof() || key.empty()) throw std::system_error(error::file_system);
    destination_address(decode_destination(key), true);
    return key;
}

void save_key(kth::path const& file, std::string const& key) {
    // Create exclusively and restrict the empty file before writing any secret.
    std::ofstream output(file, std::ios::binary | std::ios::out | std::ios::noreplace);
    if ( ! output) throw std::system_error(error::file_system);
    std::error_code ec;
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    if (ec) throw std::system_error(error::file_system);
    output << key;
    output.close();
    if ( ! output) throw std::system_error(error::file_system);
}

} // namespace

struct sam_session::impl {
    ::asio::strand<::asio::any_io_executor> strand;
    std::string bridge;
    kth::path key_file;
    bool incoming;
    std::chrono::steady_clock::duration timeout;
    std::atomic<bool> stopped{false};
    std::atomic<bool> available{false};
    std::atomic<bool> started{false};
    mutable std::mutex address_mutex;
    std::optional<authority> address;
    std::shared_ptr<socket_type> control;
    std::vector<std::weak_ptr<socket_type>> pending;
    ::asio::steady_timer retry;
    std::string id;

    void invalidate_control(sam_reply_error const& e) {
        if (e.session_lost && control) {
            available = false;
            std::error_code ignored;
            control->close(ignored);
        }
    }

    impl(::asio::any_io_executor executor, std::string bridge_, kth::path file,
        bool incoming_, std::chrono::steady_clock::duration timeout_)
        : strand(::asio::make_strand(executor)), bridge(std::move(bridge_)), key_file(std::move(file))
        , incoming(incoming_), timeout(timeout_), retry(strand)
    {}

    void disconnect() {
        available = false;
        std::error_code ignored;
        if (control) control->close(ignored);
        control.reset();
        id.clear();
        for (auto const& weak : pending) {
            if (auto socket = weak.lock()) socket->close(ignored);
        }
        pending.clear();
        std::lock_guard lock(address_mutex);
        address.reset();
    }

    ::asio::awaitable<std::shared_ptr<socket_type>> hello() {
        auto const proxy = authority::parse_from(bridge);
        if ( ! proxy || proxy->is_overlay() || proxy->port() == 0) throw std::system_error(error::illegal_value);
        auto const ip = proxy->asio_ip();
        auto const endpoint = ::asio::ip::tcp::endpoint(::asio::ip::make_address(
            ip.is_v4_mapped() ? proxy->to_hostname() : ip.to_string()), proxy->port());
        auto socket = std::make_shared<socket_type>(strand);
        std::erase_if(pending, [](auto const& weak) { return weak.expired(); });
        pending.push_back(socket);
        co_await socket->async_connect(endpoint, ::asio::use_awaitable);
        auto const reply = co_await request(*socket, "HELLO VERSION MIN=3.1 MAX=3.1", "HELLO REPLY");
        if (field(reply, "VERSION") != "3.1") invalid_reply();
        co_return socket;
    }

    awaitable_expected<void> create() {
        try {
            auto socket = co_await hello();
            auto key = incoming ? read_key(key_file) : std::optional<std::string>{};
            if (incoming && ! key) {
                auto const generated = co_await request(*socket, "DEST GENERATE SIGNATURE_TYPE=7", "DEST REPLY", false);
                key = field(generated, "PRIV");
                auto const own = destination_address(decode_destination(*key), true);
                if (destination_address(decode_destination(field(generated, "PUB")), false) != own) invalid_reply();
                save_key(key_file, *key);
            }
            auto const session_id = encode_base16(pseudo_random::generate<std::array<uint8_t, 16>>());
            auto const reply = co_await request(*socket,
                "SESSION CREATE STYLE=STREAM ID=" + session_id + " DESTINATION=" + key.value_or("TRANSIENT") +
                " SIGNATURE_TYPE=7 i2cp.leaseSetEncType=4,0", "SESSION STATUS");
            if ( ! key) key = field(reply, "DESTINATION");
            auto const own = destination_address(decode_destination(*key), true);
            if (stopped) co_return std::unexpected(error::service_stopped);
            control = std::move(socket);
            id = session_id;
            {
                std::lock_guard lock(address_mutex);
                address = own;
            }
            available = true;
            spdlog::info("[sam] I2P address: {}", own.to_hostname());
            co_return std::expected<void, code>{};
        } catch (std::system_error const& e) {
            co_return std::unexpected(sam_error_code(e));
        }
    }

    ::asio::awaitable<void> run() {
        if (started.exchange(true)) co_return;
        auto delay = std::chrono::seconds(1);
        while ( ! stopped) {
            auto const result = co_await with_deadline(create(), timeout);
            if ( ! result && ! stopped) {
                spdlog::debug("[sam] Session unavailable: {}", result.error().message());
            }
            if (result && ! stopped) {
                delay = std::chrono::seconds(1);
                char byte;
                // SAM keeps this socket idle. EOF or an unsolicited status ends the session.
                auto [ec, count] = co_await control->async_read_some(::asio::buffer(&byte, 1), ::asio::as_tuple(::asio::use_awaitable));
                (void)ec;
                (void)count;
            }
            disconnect();
            if (stopped) break;
            retry.expires_after(delay);
            auto [ec] = co_await retry.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            delay = std::min(delay * 2, std::chrono::seconds(60));
        }
        disconnect();
    }

    awaitable_expected<socket_type> connect(std::string hostname) {
        try {
            auto const peer = authority::parse_from(hostname, 0);
            if ( ! peer || ! peer->is_i2p()) co_return std::unexpected(error::illegal_value);
            if (stopped) co_return std::unexpected(error::service_stopped);
            if ( ! available) co_return std::unexpected(error::operation_failed);
            auto const session_id = id;
            auto socket = co_await hello();
            auto const lookup = co_await request(*socket, "NAMING LOOKUP NAME=" + peer->to_hostname(), "NAMING REPLY");
            auto const destination = field(lookup, "VALUE");
            if (destination_address(decode_destination(destination), false) != *peer) invalid_reply();
            co_await request(*socket, "STREAM CONNECT ID=" + session_id + " DESTINATION=" + destination + " SILENT=false", "STREAM STATUS");
            if (stopped || ! available || id != session_id) co_return std::unexpected(error::operation_failed);
            co_return std::move(*socket);
        } catch (sam_reply_error const& e) {
            invalidate_control(e);
            co_return std::unexpected(error::operation_failed);
        } catch (std::system_error const& e) {
            co_return std::unexpected(sam_error_code(e));
        }
    }

    awaitable_expected<sam_inbound> accept() {
        try {
            if (stopped) co_return std::unexpected(error::service_stopped);
            if ( ! incoming || ! available) co_return std::unexpected(error::operation_failed);
            auto const session_id = id;
            auto socket = co_await hello();
            co_await request(*socket, "STREAM ACCEPT ID=" + session_id + " SILENT=false", "STREAM STATUS");
            auto const destination = co_await read_line(*socket);
            if (destination.starts_with("STREAM STATUS")) parse_reply(destination, "STREAM STATUS");
            auto const peer = destination_address(decode_destination(destination), false);
            if (stopped || ! available || id != session_id) co_return std::unexpected(error::operation_failed);
            co_return sam_inbound{std::move(*socket), peer};
        } catch (sam_reply_error const& e) {
            invalidate_control(e);
            co_return std::unexpected(error::operation_failed);
        } catch (std::system_error const& e) {
            co_return std::unexpected(sam_error_code(e));
        }
    }
};

sam_session::sam_session(::asio::any_io_executor executor, std::string bridge,
    kth::path private_key_file, bool accept_incoming, std::chrono::steady_clock::duration timeout)
    : impl_(std::make_shared<impl>(executor, std::move(bridge), std::move(private_key_file), accept_incoming, timeout))
{}

sam_session::~sam_session() { stop(); }

::asio::awaitable<void> sam_session::run() {
    auto self = impl_;
    co_await ::asio::co_spawn(self->strand, [self]() -> ::asio::awaitable<void> {
        co_await self->run();
    }, ::asio::use_awaitable);
}

void sam_session::stop() {
    auto self = impl_;
    self->stopped = true;
    ::asio::post(self->strand, [self] {
        self->disconnect();
        self->retry.cancel();
    });
}

bool sam_session::ready() const { return impl_->available && ! impl_->stopped; }

std::optional<authority> sam_session::local_address() const {
    std::lock_guard lock(impl_->address_mutex);
    return impl_->address;
}

// co_spawn needs a default-constructible result and a socket is not one, so
// the result crosses the strand inside an optional.
awaitable_expected<socket_type> sam_session::connect(std::string hostname) {
    auto self = impl_;
    auto result = co_await ::asio::co_spawn(self->strand,
        [self, hostname]() -> ::asio::awaitable<std::optional<std::expected<socket_type, code>>> {
            co_return co_await with_deadline(self->connect(hostname), self->timeout);
        }, ::asio::use_awaitable);
    co_return std::move(*result);
}

awaitable_expected<sam_inbound> sam_session::accept() {
    auto self = impl_;
    auto result = co_await ::asio::co_spawn(self->strand,
        [self]() -> ::asio::awaitable<std::optional<std::expected<sam_inbound, code>>> {
            co_return co_await with_deadline(self->accept(), self->timeout);
        }, ::asio::use_awaitable);
    co_return std::move(*result);
}

} // namespace kth::network
