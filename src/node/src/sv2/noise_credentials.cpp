// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/noise_credentials.hpp>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <span>
#include <string>
#include <utility>

#if ! defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/node/detail/noise_file.hpp>
#include <kth/node/detail/scope_action.hpp>

namespace kth::node::sv2 {

namespace {

#if ! defined(_WIN32)

struct secret_file_operations {
    // O_EXCL and mode 0600 protect the secret from its first write, without
    // changing the process umask while other node tasks may create files.
    int open_read(std::string const& path) const {
        return ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    }
    int create(std::string const& path) const {
        return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    }
    int secure(int fd) const { return ::fchmod(fd, 0600); }
    int inspect(int fd, detail::noise_file_status& value) const {
        struct stat status{};
        auto const result = ::fstat(fd, &status);
        value = {bool(S_ISREG(status.st_mode)), status.st_uid == ::geteuid(),
            (status.st_mode & 0077) == 0, static_cast<uint64_t>(status.st_size)};
        return result;
    }
    std::ptrdiff_t read(int fd, byte_span_mut bytes) const {
        return ::read(fd, bytes.data(), bytes.size());
    }
    std::ptrdiff_t write(int fd, byte_span bytes) const {
        return ::write(fd, bytes.data(), bytes.size());
    }
    void close(int fd) const { ::close(fd); }
    int error() const { return errno; }
};

#endif

std::optional<noise_keypair> secret_file(std::string const& path, std::string& error) {
    error = "Cannot load an owner-only 32-byte Noise secret key: " + path;
#if ! defined(_WIN32)
    secret_file_operations operations;
    auto secret = detail::load_noise_secret(operations, path,
        [] { return noise_keypair::generate().secret(); });
    if ( ! secret) return std::nullopt;
    detail::scope_action erase([&] { pseudo_random::wipe(*secret); }, true);
    hash_digest entropy{};
    pseudo_random::fill_bytes(std::as_writable_bytes(std::span{entropy}));
    return noise_keypair::create(*secret, entropy);
#else
    // This path needs a platform owner-only file creation primitive before
    // Noise can store credentials on Windows. Refuse rather than expose keys.
    error = "Noise secret key files require POSIX owner-only file permissions";
    return std::nullopt;
#endif
}

std::optional<noise_certificate> certificate_file(std::string const& path) {
#if ! defined(_WIN32)
    // Public certificates need no private mode, but must be regular files too:
    // a FIFO or device must not stall startup before the listener is bound.
    secret_file_operations operations;
    auto const fd = operations.open_read(path);
    if (fd == -1) return std::nullopt;
    auto const bytes = detail::read_noise_file<noise_certificate::size>(operations, fd, false);
    if ( ! bytes) return std::nullopt;
    return noise_certificate::parse(*bytes);
#else
    (void)path;
    return std::nullopt;
#endif
}

// Clients refuse a certificate whose valid_from is ahead of their own clock.
constexpr uint32_t clock_margin_seconds = 60 * 60;

std::optional<noise_certificate> sign_certificate(noise_keypair const& authority,
    hash_digest const& static_public, uint32_t now, uint32_t validity) {
    auto const from = now - std::min(now, clock_margin_seconds);
    auto const until = now + std::min(validity, std::numeric_limits<uint32_t>::max() - now);
    hash_digest auxiliary{};
    pseudo_random::fill_bytes(std::as_writable_bytes(std::span{auxiliary}));
    return noise_certificate::sign(authority.secret(), static_public, from, until, auxiliary);
}

} // namespace

std::optional<noise_certificate> noise_credentials::certificate_at(uint32_t now) const {
    if (authority) {
        return sign_certificate(*authority, static_key.public_key(), now,
            certificate_validity_seconds);
    }
    if ( ! certificate.verify(authority_public_key, static_key.public_key(), now)) {
        return std::nullopt;
    }
    return certificate;
}

std::optional<noise_credentials> load_noise_credentials(sv2_settings const& settings,
    uint32_t now, std::string& error) {
    error.clear();
    if (settings.static_key_file.empty() || settings.handshake_timeout_seconds == 0) {
        error = "Noise needs a static key file and a positive handshake deadline";
        return std::nullopt;
    }
    auto key = secret_file(settings.static_key_file, error);
    if ( ! key) return std::nullopt;

    std::optional<noise_certificate> certificate;
    std::optional<noise_keypair> signing_key;
    hash_digest authority{};
    if ( ! settings.certificate_file.empty()) {
        auto const pinned = noise_parse_authority_key(settings.authority_public_key);
        if ( ! pinned) {
            error = "An offline Noise certificate needs a valid authority public key";
            return std::nullopt;
        }
        authority = *pinned;
        certificate = certificate_file(settings.certificate_file);
    } else {
        if (settings.certificate_validity_seconds == 0
            || settings.certificate_validity_seconds > std::numeric_limits<uint32_t>::max() - now
            || settings.authority_key_file.empty()) {
            error = "Noise needs an authority key file and a representable positive certificate lifetime";
            return std::nullopt;
        }
        signing_key = secret_file(settings.authority_key_file, error);
        if ( ! signing_key) return std::nullopt;
        authority = signing_key->public_key();
        if ( ! settings.authority_public_key.empty()
            && noise_parse_authority_key(settings.authority_public_key) != std::optional{authority}) {
            error = "The configured Noise authority public key does not match its secret key";
            return std::nullopt;
        }
        certificate = sign_certificate(*signing_key, key->public_key(), now,
            settings.certificate_validity_seconds);
    }
    if ( ! certificate || ! certificate->verify(authority, key->public_key(), now)) {
        error = "Noise certificate is invalid, outside its validity period or belongs to another static key";
        return std::nullopt;
    }
    error.clear();
    return noise_credentials{std::move(*key), *certificate, authority, std::move(signing_key),
        settings.certificate_validity_seconds};
}

} // namespace kth::node::sv2
