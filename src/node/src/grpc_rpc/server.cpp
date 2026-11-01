// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/grpc_rpc/server.hpp>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/node/grpc_rpc/chain_handlers.hpp>

namespace kth::node::grpc_rpc {

namespace {

constexpr char cookie_user[] = "__cookie__";

std::string random_token() {
    std::random_device device;
    constexpr char digits[] = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 32; ++i) {
        auto const byte = static_cast<unsigned>(device()) & 0xffu;
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0xf]);
    }
    return token;
}

std::string errno_message(char const* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

transport_options to_options(rpc_settings const& settings) {
    transport_options options;
    options.bind = settings.bind;
    options.port = settings.port;
    options.user = settings.user;
    options.password = settings.password;
    return options;
}

} // namespace

std::expected<void, std::string> write_private_file(
    std::filesystem::path const& path, std::string_view content)
{
    // A fresh name in the same directory (so the final rename is atomic), created
    // only if it does not exist, never through a link. Retrying on a name clash
    // is bounded and not a wait.
    std::random_device device;
    std::filesystem::path temporary;
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        temporary = path;
        temporary += ".tmp" + std::to_string(device());
#if defined(_WIN32)
        fd = ::_wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
            _S_IREAD | _S_IWRITE);
#else
        fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
#endif
        if (fd < 0 && errno != EEXIST) {
            return std::unexpected(errno_message("cannot create temporary file"));
        }
    }
    if (fd < 0) {
        return std::unexpected(std::string("cannot create a unique temporary file"));
    }

    auto const abandon = [&](std::string message) -> std::expected<void, std::string> {
        auto const saved = errno;
#if defined(_WIN32)
        ::_close(fd);
#else
        ::close(fd);
#endif
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        errno = saved;
        return std::unexpected(std::move(message));
    };

#if !defined(_WIN32)
    // The mode requested at creation is filtered by the umask and could be
    // anything on an unusual filesystem: state it, and verify it, before the
    // secret exists anywhere.
    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        return abandon(errno_message("cannot restrict permissions"));
    }
    struct stat info;
    if (::fstat(fd, &info) != 0 || (info.st_mode & 0777) != (S_IRUSR | S_IWUSR)) {
        return abandon("cannot confirm owner-only permissions");
    }
#endif

    char const* cursor = content.data();
    std::size_t remaining = content.size();
    while (remaining != 0) {
#if defined(_WIN32)
        auto const written = ::_write(fd, cursor, static_cast<unsigned>(remaining));
#else
        auto const written = ::write(fd, cursor, remaining);
#endif
        if (written < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
#endif
            return abandon(errno_message("cannot write"));
        }
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
#if defined(_WIN32)
    if (::_commit(fd) != 0) {
        return abandon(errno_message("cannot flush"));
    }
    if (::_close(fd) != 0) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected(errno_message("cannot close"));
    }
#else
    if (::fsync(fd) != 0) {
        return abandon(errno_message("cannot flush"));
    }
    if (::close(fd) != 0) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected(errno_message("cannot close"));
    }
#endif

    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return std::unexpected("cannot replace " + path.string() + ": " + rename_error.message());
    }
    return {};
}

server::server(rpc_settings const& settings, blockchain::block_chain& chain,
    domain::config::network network, std::filesystem::path cookie_path)
    : settings_(settings)
    , cookie_path_(std::move(cookie_path))
    , transport_(to_options(settings),
        make_chain_handlers(make_chain_reader(chain), to_proto(network), chain.executor()))
{}

std::expected<std::uint16_t, std::string> server::start() {
    if (settings_.user.empty() && settings_.password.empty()) {
        // bitcoind-style cookie. Failing closed: if the secret cannot be saved
        // privately, the server does not start rather than serve with a credential
        // nobody can read or one that anybody can.
        auto const token = random_token();
        auto const saved = write_private_file(cookie_path_, std::string(cookie_user) + ":" + token);
        if ( ! saved) {
            return std::unexpected("cannot save the private auth cookie " +
                cookie_path_.string() + ": " + saved.error());
        }
        if ( ! transport_.set_credentials(cookie_user, token)) {
            return std::unexpected(std::string("the gRPC transport already started or stopped"));
        }
        spdlog::info("[grpc] using generated auth cookie ({})", cookie_path_.string());
    }
    return transport_.start();
}

void server::stop() {
    transport_.stop();
}

void server::join() {
    transport_.join();
}

} // namespace kth::node::grpc_rpc
