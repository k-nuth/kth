// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/noise_credentials.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/node/detail/noise_file.hpp>
#include <kth/node/detail/scope_action.hpp>

namespace kth::node::sv2 {

namespace {

// Clients refuse a certificate whose valid_from is ahead of their own clock.
constexpr uint32_t clock_margin_seconds = 60 * 60;

std::expected<noise_certificate, noise_error> sign_certificate(noise_keypair const& authority,
    hash_digest const& static_public, uint32_t now, uint32_t validity) {
    auto const from = now - std::min(now, clock_margin_seconds);
    auto const until = now + std::min(validity, std::numeric_limits<uint32_t>::max() - now);
    hash_digest auxiliary{};
    pseudo_random::fill_bytes(std::as_writable_bytes(std::span{auxiliary}));
    return noise_certificate::sign(authority.secret(), static_public, from, until, auxiliary);
}

#if ! defined(_WIN32)

// The key pair of a 32-byte secret file. With `create`, a file that is not
// there is created; without, it must exist.
std::expected<noise_keypair, std::string> secret_keypair(detail::posix_noise_file_operations& operations,
    std::filesystem::path const& path, bool create) {
    std::expected<hash_digest, std::string> secret = std::unexpected(std::string{});
    if (create) {
        secret = detail::load_noise_secret(operations, path,
            [] { return noise_keypair::generate().secret(); });
    } else {
        auto const fd = operations.open_read(path);
        if (fd == -1) {
            return std::unexpected(detail::noise_file_error("cannot open", path, operations.error()));
        }
        secret = detail::read_noise_file<32>(operations, fd, path, true);
    }
    if ( ! secret) return std::unexpected(secret.error());
    detail::scope_action erase([&] { pseudo_random::wipe(*secret); }, true);
    hash_digest entropy{};
    pseudo_random::fill_bytes(std::as_writable_bytes(std::span{entropy}));
    auto key = noise_keypair::create(*secret, entropy);
    if ( ! key) return std::unexpected(path.string() + " does not hold a secp256k1 secret key");
    return std::move(*key);
}

// The certificate in a file, or the error number of an open that failed, so
// that a certificate not written yet can be told from one that cannot be read.
std::expected<noise_certificate, std::pair<int, std::string>> certificate_file(
    detail::posix_noise_file_operations& operations, std::filesystem::path const& path) {
    // A certificate is public and needs no private mode, but must be a regular
    // file too: a FIFO or a device must not stall the start.
    auto const fd = operations.open_read(path);
    if (fd == -1) {
        auto const code = operations.error();
        return std::unexpected(std::pair{code, detail::noise_file_error("cannot open", path, code)});
    }
    auto const bytes = detail::read_noise_file<noise_certificate::size>(operations, fd, path, false);
    if ( ! bytes) return std::unexpected(std::pair{0, bytes.error()});
    auto const certificate = noise_certificate::parse(*bytes);
    if ( ! certificate) {
        return std::unexpected(std::pair{0, path.string() + ": " + std::string(to_string(certificate.error()))});
    }
    return *certificate;
}

std::expected<noise_credentials, std::string> load_online(sv2_settings const& settings,
    uint32_t now, detail::posix_noise_file_operations& operations) {
    if (settings.certificate_validity_seconds == 0) {
        return std::unexpected(std::string("sv2.certificate_validity_seconds must be positive"));
    }
    if (settings.certificate_validity_seconds > std::numeric_limits<uint32_t>::max() - now) {
        return std::unexpected(std::string("sv2.certificate_validity_seconds reaches past the last "
            "certificate timestamp"));
    }
    if (settings.authority_key_file.empty()) {
        return std::unexpected(std::string("sv2.authority_key_file is empty"));
    }
    // The settings are checked before any file is created.
    std::optional<hash_digest> pinned;
    if ( ! settings.authority_public_key.empty()) {
        auto const parsed = noise_parse_authority_key(settings.authority_public_key);
        if ( ! parsed) {
            return std::unexpected("sv2.authority_public_key is " + std::string(to_string(parsed.error())));
        }
        pinned = *parsed;
    }
    auto static_key = secret_keypair(operations, settings.static_key_file, true);
    if ( ! static_key) return std::unexpected(static_key.error());
    auto authority = secret_keypair(operations, settings.authority_key_file, true);
    if ( ! authority) return std::unexpected(authority.error());
    if (pinned && *pinned != authority->public_key()) {
        return std::unexpected("sv2.authority_public_key is not the public key of the secret in "
            + settings.authority_key_file);
    }
    auto const certificate = sign_certificate(*authority, static_key->public_key(), now,
        settings.certificate_validity_seconds);
    if ( ! certificate) {
        return std::unexpected("cannot sign a certificate: " + std::string(to_string(certificate.error())));
    }
    auto const authority_public = authority->public_key();
    return noise_credentials{std::move(*static_key), authority_public, std::move(*authority),
        settings.certificate_validity_seconds, *certificate};
}

std::expected<noise_credentials, std::string> load_offline(sv2_settings const& settings,
    uint32_t now, detail::posix_noise_file_operations& operations) {
    auto const pinned = noise_parse_authority_key(settings.authority_public_key);
    if ( ! pinned) {
        return std::unexpected("sv2.certificate_file needs sv2.authority_public_key, the key that "
            "signed it; it is " + std::string(to_string(pinned.error())));
    }
    auto const certificate = certificate_file(operations, settings.certificate_file);
    if ( ! certificate && certificate.error().first == ENOENT) {
        // No certificate yet: the static key is made (or read) so that there
        // is one to sign, and its public key is the answer.
        auto const static_key = secret_keypair(operations, settings.static_key_file, true);
        if ( ! static_key) return std::unexpected(static_key.error());
        return std::unexpected("there is no certificate at " + settings.certificate_file
            + "; sign one with the authority key for the static public key "
            + encode_base16(static_key->public_key()) + " and start again");
    }
    if ( ! certificate) return std::unexpected(certificate.error().second);
    // A certificate names one static key. A missing key file is not made
    // again: a new key would never match it.
    auto static_key = secret_keypair(operations, settings.static_key_file, false);
    if ( ! static_key) {
        return std::unexpected(static_key.error() + "; the certificate in " + settings.certificate_file
            + " belongs to the static key that file held");
    }
    auto const valid = certificate->verify(*pinned, static_key->public_key(), now);
    if ( ! valid) {
        return std::unexpected("the certificate in " + settings.certificate_file + " is "
            + std::string(to_string(valid.error())) + "; sign one for the static public key "
            + encode_base16(static_key->public_key()) + " and start again");
    }
    return noise_credentials{std::move(*static_key), *pinned, std::nullopt, 0, *certificate};
}

#endif

std::filesystem::path anchored(std::string const& path, std::filesystem::path const& directory,
    char const* name) {
    if (path.empty()) return directory / name;
    std::filesystem::path const given(path);
    return given.is_relative() ? directory / given : given;
}

} // namespace

std::expected<noise_certificate, noise_error> noise_credentials::certificate_at(uint32_t now) {
    if ( ! authority) {
        auto const valid = certificate.verify(authority_public_key, static_key.public_key(), now);
        if ( ! valid) return std::unexpected(valid.error());
        return certificate;
    }
    // A clock that went back before the certificate, or one past half its
    // lifetime, has it signed again.
    auto const outside = now < certificate.valid_from || now > certificate.not_valid_after;
    if (outside || certificate.not_valid_after - now < certificate_validity_seconds / 2) {
        auto renewed = sign_certificate(*authority, static_key.public_key(), now,
            certificate_validity_seconds);
        if ( ! renewed) return std::unexpected(renewed.error());
        certificate = *renewed;
    }
    return certificate;
}

std::expected<uint32_t, noise_error> noise_clock() {
    auto const now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < 0 || now > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(noise_error::clock_out_of_range);
    }
    return uint32_t(now);
}

sv2_settings with_noise_files(sv2_settings settings, std::filesystem::path const& data_directory) {
    settings.static_key_file = anchored(settings.static_key_file, data_directory,
        "sv2_static_key").string();
    if (settings.certificate_file.empty()) {
        settings.authority_key_file = anchored(settings.authority_key_file, data_directory,
            "sv2_authority_key").string();
    } else {
        settings.certificate_file = anchored(settings.certificate_file, data_directory, "").string();
    }
    return settings;
}

std::expected<noise_credentials, std::string> load_noise_credentials(sv2_settings const& settings,
    uint32_t now) {
#if ! defined(_WIN32)
    if (settings.static_key_file.empty()) {
        return std::unexpected(std::string("sv2.static_key_file is empty"));
    }
    detail::posix_noise_file_operations operations;
    return settings.certificate_file.empty()
        ? load_online(settings, now, operations)
        : load_offline(settings, now, operations);
#else
    // Owner-only file creation has no portable equivalent here yet; keys are
    // refused rather than written readable by others.
    (void)settings;
    (void)now;
    return std::unexpected(std::string("Noise key files need POSIX owner-only permissions, "
        "which this build does not have; set sv2.noise=false"));
#endif
}

} // namespace kth::node::sv2
