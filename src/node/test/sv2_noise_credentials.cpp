// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#if ! defined(_WIN32)
#include <unistd.h>
#endif

#include <kth/node/detail/noise_file.hpp>
#include <kth/node/sv2/noise_credentials.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

struct file_operations {
    struct transfer {
        std::ptrdiff_t count;
        int error = 0;
    };

    int descriptor = 7;
    int open_result = descriptor;
    int create_result = descriptor;
    int secure_result = 0;
    int inspect_result = 0;
    int last_error = 0;
    unsigned creates = 0;
    unsigned closes = 0;
    unsigned reads = 0;
    unsigned writes = 0;
    size_t offset = 0;
    node::detail::noise_file_status status{true, true, true, 32};
    data_chunk input = data_chunk(32, 0x23);
    data_chunk output;
    std::deque<transfer> read_results;
    std::deque<transfer> write_results;

    int open_read(std::string const&) const { return open_result; }
    int create(std::string const&) { ++creates; return create_result; }
    int secure(int fd) const { REQUIRE(fd == descriptor); return secure_result; }
    int inspect(int fd, node::detail::noise_file_status& value) const {
        REQUIRE(fd == descriptor);
        value = status;
        return inspect_result;
    }
    void close(int fd) { REQUIRE(fd == descriptor); ++closes; }
    int error() const { return last_error; }

    transfer next(std::deque<transfer>& results, size_t size) {
        if (results.empty()) return {static_cast<std::ptrdiff_t>(size)};
        auto const result = results.front();
        results.pop_front();
        last_error = result.error;
        return result;
    }
    std::ptrdiff_t read(int fd, byte_span_mut bytes) {
        REQUIRE(fd == descriptor);
        ++reads;
        auto const result = next(read_results, bytes.size());
        if (result.count > 0) {
            auto const count = static_cast<size_t>(result.count);
            REQUIRE(count <= bytes.size());
            REQUIRE(offset + count <= input.size());
            std::copy_n(input.begin() + offset, count, bytes.begin());
            offset += count;
        }
        return result.count;
    }
    std::ptrdiff_t write(int fd, byte_span bytes) {
        REQUIRE(fd == descriptor);
        ++writes;
        auto const result = next(write_results, bytes.size());
        if (result.count > 0) {
            auto const count = static_cast<size_t>(result.count);
            REQUIRE(count <= bytes.size());
            output.insert(output.end(), bytes.begin(), bytes.begin() + count);
        }
        return result.count;
    }
};

hash_digest generated_secret() {
    hash_digest secret;
    for (size_t i = 0; i < secret.size(); ++i) secret[i] = static_cast<uint8_t>(i + 1);
    return secret;
}

#if ! defined(_WIN32)

noise_keypair fixed_key(uint8_t value) {
    hash_digest secret{};
    secret.back() = value;
    auto const key = noise_keypair::create(secret, hash_digest{});
    REQUIRE(key.has_value());
    return *key;
}

struct credential_files {
    std::filesystem::path directory;
    node::sv2_settings settings;

    credential_files() {
        static unsigned next = 0;
        directory = std::filesystem::temp_directory_path()
            / ("kth_sv2_noise_" + std::to_string(::getpid()) + "_" + std::to_string(++next));
        REQUIRE(std::filesystem::create_directory(directory));
        settings.static_key_file = (directory / "static.key").string();
        settings.authority_key_file = (directory / "authority.key").string();
    }

    ~credential_files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    void write(std::string const& path, byte_span bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        output.close();
        REQUIRE(output.good());
        std::filesystem::permissions(path, std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
    }

    void offline(noise_certificate const& certificate, hash_digest const& authority) {
        settings.certificate_file = (directory / "static.cert").string();
        settings.authority_public_key = noise_authority_key(authority);
        write(settings.certificate_file, certificate.bytes());
    }
};

#endif

} // namespace

TEST_CASE("Noise secret file IO refuses failures and closes every acquired descriptor", "[sv2 noise]") {
    file_operations operations;
    unsigned expected_closes = 1;
    unsigned generated = 0;
    SECTION("read open fails without creating a replacement") {
        operations.open_result = -1;
        operations.last_error = EACCES;
        expected_closes = 0;
    }
    SECTION("exclusive create fails") {
        operations.open_result = -1;
        operations.last_error = ENOENT;
        operations.create_result = -1;
        expected_closes = 0;
    }
    SECTION("permissions cannot be secured") {
        operations.open_result = -1;
        operations.last_error = ENOENT;
        operations.secure_result = -1;
    }
    SECTION("file metadata cannot be read") {
        operations.inspect_result = -1;
    }
    SECTION("read fails") {
        operations.read_results = {{-1, EIO}};
    }
    SECTION("read ends early") {
        operations.read_results = {{8}, {0}};
    }
    SECTION("write fails") {
        operations.open_result = -1;
        operations.last_error = ENOENT;
        operations.write_results = {{-1, ENOSPC}};
    }
    SECTION("write makes no progress") {
        operations.open_result = -1;
        operations.last_error = ENOENT;
        operations.write_results = {{8}, {0}};
    }
    auto const result = node::detail::load_noise_secret(operations, "key", [&] {
        ++generated;
        return generated_secret();
    });
    CHECK_FALSE(result.has_value());
    CHECK(operations.closes == expected_closes);
    CHECK(generated == operations.creates);
    if (operations.open_result != -1 || operations.last_error == EACCES) {
        CHECK(operations.creates == 0);
        CHECK(operations.writes == 0);
    }
}

TEST_CASE("Noise secret file IO retries interruptions and completes short transfers", "[sv2 noise]") {
    file_operations operations;
    auto const expected = generated_secret();
    SECTION("existing secret is read without regenerating it") {
        operations.input.assign(expected.begin(), expected.end());
        operations.read_results = {{-1, EINTR}, {5}, {-1, EINTR}, {27}};
    }
    SECTION("new secret is written once across interrupted partial writes") {
        operations.open_result = -1;
        operations.last_error = ENOENT;
        operations.write_results = {{-1, EINTR}, {7}, {-1, EINTR}, {25}};
    }
    unsigned generated = 0;
    auto const result = node::detail::load_noise_secret(operations, "key", [&] {
        ++generated;
        return expected;
    });
    REQUIRE(result.has_value());
    CHECK(*result == expected);
    CHECK(operations.closes == 1);
    if (operations.open_result == -1) {
        CHECK(generated == 1);
        CHECK(operations.creates == 1);
        CHECK(operations.writes == 4);
        CHECK(operations.output == to_chunk(expected));
    } else {
        CHECK(generated == 0);
        CHECK(operations.creates == 0);
        CHECK(operations.reads == 4);
        CHECK(operations.writes == 0);
    }
}

TEST_CASE("Noise file IO validates metadata before reading a secret or public certificate", "[sv2 noise]") {
    for (int fault = 0; fault < 4; ++fault) {
        CAPTURE(fault);
        file_operations operations;
        if (fault == 0) operations.status.regular = false;
        if (fault == 1) operations.status.owned = false;
        if (fault == 2) operations.status.private_file = false;
        if (fault == 3) operations.status.size = 33;
        CHECK_FALSE(node::detail::load_noise_secret(operations, "key", generated_secret).has_value());
        CHECK(operations.reads == 0);
        CHECK(operations.creates == 0);
        CHECK(operations.closes == 1);
    }
    file_operations public_file;
    public_file.status = {true, false, false, noise_certificate::size};
    public_file.input.resize(noise_certificate::size);
    CHECK(node::detail::read_noise_file<noise_certificate::size>(public_file,
        public_file.descriptor, false).has_value());
    CHECK(public_file.closes == 1);
}

#if ! defined(_WIN32)

TEST_CASE("Noise first start creates owner-only keys and preserves them on restart", "[sv2 noise]") {
    credential_files files;
    std::string error;
    auto const first = load_noise_credentials(files.settings, 100, error);
    REQUIRE(first.has_value());
    CHECK(error.empty());
    // The hour of clock margin cannot start before the epoch.
    CHECK(first->certificate.valid_from == 0u);
    CHECK(first->certificate.not_valid_after == 100u + files.settings.certificate_validity_seconds);
    for (auto const& path : {files.settings.static_key_file, files.settings.authority_key_file}) {
        CHECK(std::filesystem::file_size(path) == 32u);
        auto const permissions = std::filesystem::status(path).permissions();
        CHECK((permissions & std::filesystem::perms::all)
            == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }
    auto const second = load_noise_credentials(files.settings, 101, error);
    REQUIRE(second.has_value());
    CHECK(second->static_key.secret() == first->static_key.secret());
    CHECK(second->authority_public_key == first->authority_public_key);
    CHECK(second->certificate.verify(second->authority_public_key, second->static_key.public_key(), 101));
}

TEST_CASE("Noise online authority signs a current certificate long after startup", "[sv2 noise]") {
    credential_files files;
    std::string error;
    constexpr uint32_t start = 1'700'000'000;
    auto const credentials = load_noise_credentials(files.settings, start, error);
    REQUIRE(credentials.has_value());
    REQUIRE(credentials->authority.has_value());
    auto const validity = files.settings.certificate_validity_seconds;
    auto const& authority = credentials->authority_public_key;
    auto const& provider = credentials->static_key.public_key();

    auto const later = start + 10 * validity;
    CHECK_FALSE(credentials->certificate.verify(authority, provider, later));
    auto const current = credentials->certificate_at(later);
    REQUIRE(current.has_value());
    CHECK(current->valid_from == later - 3600);
    CHECK(current->not_valid_after == later + validity);
    CHECK(current->verify(authority, provider, later));
    // A client whose clock is up to an hour behind the provider's accepts it,
    // and the client's own leeway comes on top of that.
    CHECK(current->verify(authority, provider, later - 3600));
    CHECK(current->verify(authority, provider, later - 3600 - certificate_clock_leeway_seconds));
    CHECK_FALSE(current->verify(authority, provider, later - 3601 - certificate_clock_leeway_seconds));

    // Near the end of the timestamp range the lifetime is cut, never wrapped.
    auto const last = credentials->certificate_at(std::numeric_limits<uint32_t>::max() - 1);
    REQUIRE(last.has_value());
    CHECK(last->not_valid_after == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("Noise refuses malformed and insecure existing secrets without replacing them", "[sv2 noise]") {
    credential_files files;
    std::string error;
    for (auto const length : {0u, 31u, 32u, 33u}) {
        data_chunk invalid(length);
        files.write(files.settings.static_key_file, invalid);
        CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
        CHECK_FALSE(error.empty());
        CHECK(std::filesystem::file_size(files.settings.static_key_file) == length);
    }
    files.write(files.settings.static_key_file, fixed_key(1).secret());
    std::filesystem::permissions(files.settings.static_key_file,
        std::filesystem::perms::group_read, std::filesystem::perm_options::add);
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());

    std::filesystem::remove(files.settings.static_key_file);
    std::filesystem::create_symlink("missing-target", files.settings.static_key_file);
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    CHECK_FALSE(std::filesystem::exists(files.directory / "missing-target"));
    std::filesystem::remove(files.settings.static_key_file);
    REQUIRE(std::filesystem::create_directory(files.settings.static_key_file));
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings.static_key_file = (files.directory / "missing-parent" / "static.key").string();
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
}

TEST_CASE("Noise refuses invalid deadlines lifetimes and authority configuration", "[sv2 noise]") {
    credential_files files;
    std::string error;
    auto original = files.settings;
    files.settings.static_key_file.clear();
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings = original;
    files.settings.handshake_timeout_seconds = 0;
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings = original;
    files.settings.certificate_validity_seconds = 0;
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings = original;
    files.settings.certificate_validity_seconds = std::numeric_limits<uint32_t>::max();
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings = original;
    files.settings.authority_key_file.clear();
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings = original;
    files.write(files.settings.authority_key_file, data_chunk(32));
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.write(files.settings.authority_key_file, fixed_key(2).secret());
    files.settings.authority_public_key = noise_authority_key(fixed_key(3).public_key());
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings.authority_public_key = "invalid";
    CHECK_FALSE(load_noise_credentials(files.settings, 100, error).has_value());
    files.settings.authority_public_key = noise_authority_key(fixed_key(2).public_key());
    CHECK(load_noise_credentials(files.settings, 100, error).has_value());
}

TEST_CASE("Noise accepts offline certificates without opening an authority secret", "[sv2 noise]") {
    credential_files files;
    auto const provider = fixed_key(1);
    auto const authority = fixed_key(2);
    files.write(files.settings.static_key_file, provider.secret());
    auto const certificate = noise_certificate::sign(authority.secret(), provider.public_key(), 100, 200);
    REQUIRE(certificate.has_value());
    files.offline(*certificate, authority.public_key());
    std::string error;
    auto const loaded = load_noise_credentials(files.settings, 150, error);
    REQUIRE(loaded.has_value());
    CHECK(loaded->certificate.bytes() == certificate->bytes());
    CHECK(loaded->authority_public_key == authority.public_key());
    CHECK_FALSE(loaded->authority.has_value());
    CHECK_FALSE(std::filesystem::exists(files.settings.authority_key_file));
    // Handshakes send the operator's certificate unchanged, and only inside its
    // validity period.
    auto const served = loaded->certificate_at(200);
    REQUIRE(served.has_value());
    CHECK(served->bytes() == certificate->bytes());
    CHECK_FALSE(loaded->certificate_at(201).has_value());

    for (auto const now : {99u, 201u}) {
        CHECK_FALSE(load_noise_credentials(files.settings, now, error).has_value());
    }
    files.settings.authority_public_key.clear();
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    files.settings.authority_public_key = "invalid";
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    files.settings.authority_public_key = noise_authority_key(fixed_key(3).public_key());
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    files.settings.authority_public_key = noise_authority_key(authority.public_key());
    files.write(files.settings.static_key_file, fixed_key(3).secret());
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
}

TEST_CASE("Noise refuses missing malformed and tampered offline certificates", "[sv2 noise]") {
    credential_files files;
    auto const provider = fixed_key(1);
    auto const authority = fixed_key(2);
    files.write(files.settings.static_key_file, provider.secret());
    auto const certificate = noise_certificate::sign(authority.secret(), provider.public_key(), 100, 200);
    REQUIRE(certificate.has_value());
    files.offline(*certificate, authority.public_key());
    std::string error;
    std::filesystem::remove(files.settings.certificate_file);
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    for (auto const size : {73u, 75u}) {
        auto bytes = to_chunk(certificate->bytes());
        bytes.resize(size);
        files.write(files.settings.certificate_file, bytes);
        CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    }
    auto bytes = certificate->bytes();
    bytes.back() ^= 1;
    files.write(files.settings.certificate_file, bytes);
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    bytes = certificate->bytes();
    bytes.front() = 1;
    files.write(files.settings.certificate_file, bytes);
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());

    std::filesystem::remove(files.settings.certificate_file);
    REQUIRE(std::filesystem::create_directory(files.settings.certificate_file));
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
    files.settings.certificate_file = "/dev/null";
    CHECK_FALSE(load_noise_credentials(files.settings, 150, error).has_value());
}

#else

TEST_CASE("Noise credential loading refuses unsupported file permission semantics", "[sv2 noise]") {
    node::sv2_settings settings;
    std::string error;
    CHECK_FALSE(load_noise_credentials(settings, 100, error).has_value());
    CHECK_FALSE(error.empty());
}

#endif
