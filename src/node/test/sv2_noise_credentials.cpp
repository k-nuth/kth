// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#if ! defined(_WIN32)
#include <unistd.h>
#endif

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/node/detail/noise_file.hpp>
#include <kth/node/sv2/noise_credentials.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

// Every system call a secret file takes, scripted.
struct file_operations {
    struct transfer {
        std::ptrdiff_t count;
        int error = 0;
    };

    int descriptor = 7;
    int open_result = descriptor;
    int open_error = 0;
    int temporary_error = 0;
    int secure_result = 0;
    int inspect_error = 0;
    int sync_result = 0;
    int link_result = 0;
    int link_error = 0;
    int last_error = 0;
    int temporaries = 0;
    int closes = 0;
    int reads = 0;
    int writes = 0;
    int links = 0;
    int unlinks = 0;
    size_t offset = 0;
    node::detail::noise_file_status status{true, true, true, 32};
    data_chunk input = data_chunk(32, 0x23);
    data_chunk output;
    std::deque<transfer> read_results;
    std::deque<transfer> write_results;

    int open_read(std::filesystem::path const&) {
        last_error = open_error;
        return open_result;
    }
    std::expected<std::pair<int, std::filesystem::path>, int> create_temporary(
        std::filesystem::path const& path) {
        ++temporaries;
        if (temporary_error != 0) return std::unexpected(temporary_error);
        auto name = path;
        name += ".tmp";
        return std::pair{descriptor, name};
    }
    int secure(int fd) const {
        REQUIRE(fd == descriptor);
        return secure_result;
    }
    std::expected<node::detail::noise_file_status, int> inspect(int fd) const {
        REQUIRE(fd == descriptor);
        if (inspect_error != 0) return std::unexpected(inspect_error);
        return status;
    }
    int sync(int fd) {
        REQUIRE(fd == descriptor);
        last_error = EIO;
        return sync_result;
    }
    int close(int fd) {
        REQUIRE(fd == descriptor);
        ++closes;
        return 0;
    }
    int link(std::filesystem::path const&, std::filesystem::path const&) {
        ++links;
        last_error = link_error;
        return link_result;
    }
    int unlink(std::filesystem::path const&) {
        ++unlinks;
        return 0;
    }
    int sync_directory(std::filesystem::path const&) const { return 0; }
    int error() const { return last_error; }

    transfer next(std::deque<transfer>& results, size_t size) {
        if (results.empty()) return {std::ptrdiff_t(size)};
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
            auto const count = size_t(result.count);
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
            auto const count = size_t(result.count);
            REQUIRE(count <= bytes.size());
            output.insert(output.end(), bytes.begin(), bytes.begin() + count);
        }
        return result.count;
    }

    // No file at the key's path yet.
    void absent() {
        open_result = -1;
        open_error = ENOENT;
    }
};

hash_digest generated_secret() {
    hash_digest secret;
    for (size_t i = 0; i < secret.size(); ++i) secret[i] = uint8_t(i + 1);
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
        static int next = 0;
        directory = std::filesystem::temp_directory_path()
            / ("kth_sv2_noise_" + std::to_string(::getpid()) + "_" + std::to_string(++next));
        REQUIRE(std::filesystem::create_directory(directory));
        settings.noise = true;
        settings.static_key_file = (directory / "static.key").string();
        settings.authority_key_file = (directory / "authority.key").string();
    }

    ~credential_files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    void write(std::string const& path, byte_span bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
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

    // The names in the directory, so that a test sees what a failure left.
    std::vector<std::string> names() const {
        std::vector<std::string> found;
        for (auto const& entry : std::filesystem::directory_iterator(directory)) {
            found.push_back(entry.path().filename().string());
        }
        std::sort(found.begin(), found.end());
        return found;
    }
};

// The node's own operations, with one step made to fail.
struct failing_operations : node::detail::posix_noise_file_operations {
    enum class step { none, write, short_write, sync, link, sync_directory };
    step failing = step::none;
    int code = 0;

    std::ptrdiff_t write(int fd, byte_span bytes) {
        if (failing == step::write) {
            errno = ENOSPC;
            return -1;
        }
        if (failing == step::short_write) {
            // A first write of 8 bytes, then no progress, as on a full disk.
            failing = step::write;
            return posix_noise_file_operations::write(fd, bytes.first(8));
        }
        return posix_noise_file_operations::write(fd, bytes);
    }
    int sync(int fd) {
        if (failing == step::sync) {
            errno = EIO;
            return -1;
        }
        return posix_noise_file_operations::sync(fd);
    }
    int link(std::filesystem::path const& from, std::filesystem::path const& to) {
        if (failing == step::link) {
            errno = EACCES;
            return -1;
        }
        return posix_noise_file_operations::link(from, to);
    }
    int sync_directory(std::filesystem::path const& path) {
        if (failing == step::sync_directory) {
            errno = EIO;
            return -1;
        }
        return posix_noise_file_operations::sync_directory(path);
    }
};

#endif

} // namespace

TEST_CASE("Noise secret file IO refuses failures and releases what it took", "[sv2 noise]") {
    file_operations operations;
    int expected_closes = 1;
    int expected_unlinks = 1;
    int generated = 0;
    SECTION("read open fails without creating a replacement") {
        operations.open_result = -1;
        operations.open_error = EACCES;
        expected_closes = 0;
        expected_unlinks = 0;
    }
    SECTION("the temporary file cannot be created") {
        operations.absent();
        operations.temporary_error = EACCES;
        expected_closes = 0;
        expected_unlinks = 0;
    }
    SECTION("permissions cannot be secured") {
        operations.absent();
        operations.secure_result = -1;
    }
    SECTION("file metadata cannot be read") {
        operations.inspect_error = EIO;
        expected_unlinks = 0;
    }
    SECTION("read fails") {
        operations.read_results = {{-1, EIO}};
        expected_unlinks = 0;
    }
    SECTION("read ends early") {
        operations.read_results = {{8}, {0}};
        expected_unlinks = 0;
    }
    SECTION("write fails") {
        operations.absent();
        operations.write_results = {{-1, ENOSPC}};
    }
    SECTION("write makes no progress") {
        operations.absent();
        operations.write_results = {{8}, {0}};
    }
    SECTION("flush fails") {
        operations.absent();
        operations.sync_result = -1;
    }
    SECTION("link fails") {
        operations.absent();
        operations.link_result = -1;
        operations.link_error = EACCES;
    }
    auto const result = node::detail::load_noise_secret(operations, "key", [&] {
        ++generated;
        return generated_secret();
    });
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
    CHECK(operations.closes == expected_closes);
    // A temporary file that was made is removed again; the key's own name is
    // linked only once the secret is on disk.
    CHECK(operations.unlinks == expected_unlinks);
    CHECK(generated == operations.temporaries);
    if (operations.link_result == 0) CHECK(operations.links == 0);
    if (operations.open_result != -1 || operations.open_error == EACCES) {
        CHECK(operations.temporaries == 0);
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
        operations.absent();
        operations.write_results = {{-1, EINTR}, {7}, {-1, EINTR}, {25}};
    }
    int generated = 0;
    auto const result = node::detail::load_noise_secret(operations, "key", [&] {
        ++generated;
        return expected;
    });
    REQUIRE(result.has_value());
    CHECK(*result == expected);
    CHECK(operations.closes == 1);
    if (operations.open_result == -1) {
        CHECK(generated == 1);
        CHECK(operations.temporaries == 1);
        CHECK(operations.writes == 4);
        CHECK(operations.links == 1);
        CHECK(operations.unlinks == 1);
        CHECK(operations.output == to_chunk(expected));
    } else {
        CHECK(generated == 0);
        CHECK(operations.temporaries == 0);
        CHECK(operations.reads == 4);
        CHECK(operations.writes == 0);
    }
}

TEST_CASE("Noise secret file IO takes the key another start created first", "[sv2 noise]") {
    // The link finds a file at the key's path: another process created it
    // between this one's open and its link. That file is read, not replaced.
    file_operations operations;
    operations.absent();
    operations.link_result = -1;
    operations.link_error = EEXIST;
    int opens = 0;
    struct second_open : file_operations {
        int* opens;
        int open_read(std::filesystem::path const& path) {
            if (++*opens == 2) {
                open_result = descriptor;
                open_error = 0;
            }
            return file_operations::open_read(path);
        }
    } racing{operations, &opens};
    auto const result = node::detail::load_noise_secret(racing, "key", generated_secret);
    REQUIRE(result.has_value());
    CHECK(*result == hash_digest{0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
                                 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
                                 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
                                 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23});
    CHECK(opens == 2);
    CHECK(racing.unlinks == 1);
}

TEST_CASE("Noise file IO validates metadata before reading a secret or public certificate", "[sv2 noise]") {
    for (int fault = 0; fault < 4; ++fault) {
        CAPTURE(fault);
        file_operations operations;
        if (fault == 0) operations.status.regular = false;
        if (fault == 1) operations.status.owned = false;
        if (fault == 2) operations.status.private_file = false;
        if (fault == 3) operations.status.size = 33;
        auto const result = node::detail::load_noise_secret(operations, "key", generated_secret);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find("key") != std::string::npos);
        CHECK(operations.reads == 0);
        CHECK(operations.temporaries == 0);
        CHECK(operations.closes == 1);
    }
    file_operations public_file;
    public_file.status = {true, false, false, noise_certificate::size};
    public_file.input.resize(noise_certificate::size);
    CHECK(node::detail::read_noise_file<noise_certificate::size>(public_file,
        public_file.descriptor, "certificate", false).has_value());
    CHECK(public_file.closes == 1);
}

TEST_CASE("Noise key paths are taken from the data directory", "[sv2 noise]") {
    std::filesystem::path const data("/var/lib/kth");
    node::sv2_settings settings;
    auto const defaults = with_noise_files(settings, data);
    CHECK(defaults.static_key_file == (data / "sv2_static_key").string());
    CHECK(defaults.authority_key_file == (data / "sv2_authority_key").string());
    CHECK(defaults.certificate_file.empty());

    auto const absolute = (std::filesystem::temp_directory_path() / "authority.key").string();
    settings.static_key_file = "keys/provider.key";
    settings.authority_key_file = absolute;
    auto const named = with_noise_files(settings, data);
    CHECK(named.static_key_file == (data / "keys/provider.key").string());
    CHECK(named.authority_key_file == absolute);

    // With an offline certificate the authority key file is not read.
    settings.authority_key_file.clear();
    settings.certificate_file = "provider.cert";
    auto const offline = with_noise_files(settings, data);
    CHECK(offline.certificate_file == (data / "provider.cert").string());
    CHECK(offline.authority_key_file.empty());
}

#if ! defined(_WIN32)

TEST_CASE("Noise first start creates owner-only keys and preserves them on restart", "[sv2 noise]") {
    credential_files files;
    auto const first = load_noise_credentials(files.settings, 100);
    REQUIRE(first.has_value());
    // The hour of clock margin cannot start before the epoch.
    CHECK(first->certificate.valid_from == 0u);
    CHECK(first->certificate.not_valid_after == 100u + files.settings.certificate_validity_seconds);
    for (auto const& path : {files.settings.static_key_file, files.settings.authority_key_file}) {
        CHECK(std::filesystem::file_size(path) == 32u);
        auto const permissions = std::filesystem::status(path).permissions();
        CHECK((permissions & std::filesystem::perms::all)
            == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }
    // No temporary file is left beside the keys.
    CHECK(files.names() == std::vector<std::string>{"authority.key", "static.key"});
    auto const second = load_noise_credentials(files.settings, 101);
    REQUIRE(second.has_value());
    CHECK(second->static_key.secret() == first->static_key.secret());
    CHECK(second->authority_public_key == first->authority_public_key);
    CHECK(second->certificate.verify(second->authority_public_key, second->static_key.public_key(), 101));
}

TEST_CASE("Noise leaves no key behind when its write fails", "[sv2 noise]") {
    using step = failing_operations::step;
    for (auto const failing : {step::write, step::short_write, step::sync, step::link}) {
        CAPTURE(int(failing));
        credential_files files;
        auto const path = std::filesystem::path(files.settings.static_key_file);
        failing_operations operations;
        operations.failing = failing;
        auto const failed = node::detail::load_noise_secret(operations, path, generated_secret);
        REQUIRE_FALSE(failed.has_value());
        CHECK_FALSE(failed.error().empty());
        // Neither the key's name nor a temporary file is left: the next start
        // creates the key instead of refusing a short file.
        CHECK(files.names().empty());
        node::detail::posix_noise_file_operations working;
        auto const created = node::detail::load_noise_secret(working, path, generated_secret);
        REQUIRE(created.has_value());
        CHECK(*created == generated_secret());
        CHECK(std::filesystem::file_size(path) == 32u);
        CHECK(files.names() == std::vector<std::string>{"static.key"});
    }

    // Once the key is linked and only the flush of its directory fails, the
    // key is whole on disk: the start fails, and the next one reads it.
    credential_files files;
    auto const path = std::filesystem::path(files.settings.static_key_file);
    failing_operations operations;
    operations.failing = step::sync_directory;
    REQUIRE_FALSE(node::detail::load_noise_secret(operations, path, generated_secret).has_value());
    CHECK(files.names() == std::vector<std::string>{"static.key"});
    node::detail::posix_noise_file_operations working;
    auto const read = node::detail::load_noise_secret(working, path, [] { return hash_digest{}; });
    REQUIRE(read.has_value());
    CHECK(*read == generated_secret());
}

TEST_CASE("Noise online authority signs at start-up and again past half a lifetime", "[sv2 noise]") {
    credential_files files;
    constexpr uint32_t start = 1'700'000'000;
    auto credentials = load_noise_credentials(files.settings, start);
    REQUIRE(credentials.has_value());
    REQUIRE(credentials->authority.has_value());
    auto const validity = files.settings.certificate_validity_seconds;
    auto const& authority = credentials->authority_public_key;
    auto const& provider = credentials->static_key.public_key();

    // The certificate signed at start-up serves every handshake for half its
    // lifetime: no handshake signs.
    auto const signed_at_start = credentials->certificate.bytes();
    for (auto const now : {start, start + 1, start + validity / 2}) {
        CAPTURE(now);
        auto const served = credentials->certificate_at(now);
        REQUIRE(served.has_value());
        CHECK(served->bytes() == signed_at_start);
    }
    // Past half of it, a new one with a full lifetime ahead.
    auto const renewed = credentials->certificate_at(start + validity / 2 + 1);
    REQUIRE(renewed.has_value());
    CHECK(renewed->bytes() != signed_at_start);
    CHECK(renewed->valid_from == start + validity / 2 + 1 - 3600);
    CHECK(renewed->not_valid_after == start + validity / 2 + 1 + validity);
    CHECK(renewed->verify(authority, provider, start + validity / 2 + 1));
    auto const kept = credentials->certificate_at(start + validity / 2 + 2);
    REQUIRE(kept.has_value());
    CHECK(kept->bytes() == renewed->bytes());

    // Long after start-up, a current one.
    auto const later = start + 10 * validity;
    auto const current = credentials->certificate_at(later);
    REQUIRE(current.has_value());
    CHECK(current->valid_from == later - 3600);
    CHECK(current->not_valid_after == later + validity);
    CHECK(current->verify(authority, provider, later));
    // A client whose clock is up to an hour behind the provider's accepts it.
    CHECK(current->verify(authority, provider, later - 3600));
    CHECK_FALSE(current->verify(authority, provider, later - 3601));

    // A clock that went back before the certificate has it signed again.
    auto const back = credentials->certificate_at(later - 7200);
    REQUIRE(back.has_value());
    CHECK(back->verify(authority, provider, later - 7200));

    // Near the end of the timestamp range the lifetime is cut, never wrapped.
    auto const last = credentials->certificate_at(std::numeric_limits<uint32_t>::max() - 1);
    REQUIRE(last.has_value());
    CHECK(last->not_valid_after == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("Noise refuses malformed and insecure existing secrets without replacing them", "[sv2 noise]") {
    credential_files files;
    for (auto const length : {size_t{0}, size_t{31}, size_t{32}, size_t{33}}) {
        data_chunk invalid(length);
        files.write(files.settings.static_key_file, invalid);
        auto const refused = load_noise_credentials(files.settings, 100);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().find(files.settings.static_key_file) != std::string::npos);
        CHECK(std::filesystem::file_size(files.settings.static_key_file) == length);
    }
    files.write(files.settings.static_key_file, fixed_key(1).secret());
    std::filesystem::permissions(files.settings.static_key_file,
        std::filesystem::perms::group_read, std::filesystem::perm_options::add);
    CHECK_FALSE(load_noise_credentials(files.settings, 100).has_value());

    std::filesystem::remove(files.settings.static_key_file);
    REQUIRE(std::filesystem::create_directory(files.settings.static_key_file));
    CHECK_FALSE(load_noise_credentials(files.settings, 100).has_value());
    files.settings.static_key_file = (files.directory / "missing-parent" / "static.key").string();
    CHECK_FALSE(load_noise_credentials(files.settings, 100).has_value());
}

TEST_CASE("Noise refuses a key file that is a symbolic link to a valid key", "[sv2 noise]") {
    // The target is a valid owner-only key: only the link refuses it, so the
    // case fails without O_NOFOLLOW.
    credential_files files;
    auto const target = files.directory / "real.key";
    files.write(target.string(), fixed_key(1).secret());
    std::filesystem::create_symlink(target, files.settings.static_key_file);
    auto const refused = load_noise_credentials(files.settings, 100);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find(files.settings.static_key_file) != std::string::npos);
    // The key it points to is left as it was, and the link is not replaced.
    CHECK(std::filesystem::file_size(target) == 32u);
    CHECK(std::filesystem::is_symlink(files.settings.static_key_file));
    // The same key, not linked, is taken.
    std::filesystem::remove(files.settings.static_key_file);
    files.write(files.settings.static_key_file, fixed_key(1).secret());
    auto const loaded = load_noise_credentials(files.settings, 100);
    REQUIRE(loaded.has_value());
    CHECK(loaded->static_key.public_key() == fixed_key(1).public_key());
}

TEST_CASE("Noise refuses unusable settings before it creates any file", "[sv2 noise]") {
    credential_files files;
    auto const original = files.settings;
    auto const refused = [&](char const* expected) {
        auto const result = load_noise_credentials(files.settings, 100);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().find(expected) != std::string::npos);
        CHECK(files.names().empty());
        files.settings = original;
    };
    files.settings.static_key_file.clear();
    refused("sv2.static_key_file");
    files.settings.certificate_validity_seconds = 0;
    refused("sv2.certificate_validity_seconds");
    files.settings.certificate_validity_seconds = std::numeric_limits<uint32_t>::max();
    refused("sv2.certificate_validity_seconds");
    files.settings.authority_key_file.clear();
    refused("sv2.authority_key_file");
    files.settings.authority_public_key = "invalid";
    refused("sv2.authority_public_key");
    files.settings.certificate_file = (files.directory / "provider.cert").string();
    refused("sv2.authority_public_key");
}

TEST_CASE("Noise refuses an authority key that does not match its pinned public key", "[sv2 noise]") {
    credential_files files;
    files.write(files.settings.authority_key_file, data_chunk(32));
    CHECK_FALSE(load_noise_credentials(files.settings, 100).has_value());
    files.write(files.settings.authority_key_file, fixed_key(2).secret());
    files.settings.authority_public_key = noise_authority_key(fixed_key(3).public_key());
    auto const mismatch = load_noise_credentials(files.settings, 100);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().find(files.settings.authority_key_file) != std::string::npos);
    files.settings.authority_public_key = noise_authority_key(fixed_key(2).public_key());
    auto const loaded = load_noise_credentials(files.settings, 100);
    REQUIRE(loaded.has_value());
    CHECK(loaded->authority_public_key == fixed_key(2).public_key());
}

TEST_CASE("Noise accepts offline certificates without opening an authority secret", "[sv2 noise]") {
    credential_files files;
    auto const provider = fixed_key(1);
    auto const authority = fixed_key(2);
    files.write(files.settings.static_key_file, provider.secret());
    auto const certificate = noise_certificate::sign(authority.secret(), provider.public_key(), 100, 200);
    REQUIRE(certificate.has_value());
    files.offline(*certificate, authority.public_key());
    auto loaded = load_noise_credentials(files.settings, 150);
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
    auto const expired = loaded->certificate_at(201);
    REQUIRE_FALSE(expired.has_value());
    CHECK(expired.error() == noise_error::certificate_out_of_date);

    for (auto const now : {uint32_t{99}, uint32_t{201}}) {
        auto const refused = load_noise_credentials(files.settings, now);
        REQUIRE_FALSE(refused.has_value());
        // The error names the key the operator signs a new certificate for.
        CHECK(refused.error().find(encode_base16(provider.public_key())) != std::string::npos);
    }
    files.settings.authority_public_key.clear();
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    files.settings.authority_public_key = "invalid";
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    files.settings.authority_public_key = noise_authority_key(fixed_key(3).public_key());
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    files.settings.authority_public_key = noise_authority_key(authority.public_key());
    files.write(files.settings.static_key_file, fixed_key(3).secret());
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
}

TEST_CASE("Noise offline mode names the static key to sign and makes it only without a certificate", "[sv2 noise]") {
    credential_files files;
    auto const authority = fixed_key(2);
    files.settings.certificate_file = (files.directory / "static.cert").string();
    files.settings.authority_public_key = noise_authority_key(authority.public_key());

    // No certificate yet: the static key is made so that there is one to
    // sign, and the error gives its public key.
    auto const first = load_noise_credentials(files.settings, 150);
    REQUIRE_FALSE(first.has_value());
    CHECK(files.names() == std::vector<std::string>{"static.key"});
    data_chunk secret(32);
    {
        std::ifstream input(files.settings.static_key_file, std::ios::binary);
        input.read(reinterpret_cast<char*>(secret.data()), std::streamsize(secret.size()));
        REQUIRE(input.good());
    }
    hash_digest key_secret;
    std::copy(secret.begin(), secret.end(), key_secret.begin());
    auto const provider = noise_keypair::create(key_secret, {});
    REQUIRE(provider.has_value());
    CHECK(first.error().find(encode_base16(provider->public_key())) != std::string::npos);
    // The same key again on the next start.
    auto const again = load_noise_credentials(files.settings, 150);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error() == first.error());

    // The operator signs that key offline; the node starts with it.
    auto const certificate = noise_certificate::sign(authority.secret(), provider->public_key(), 100, 200);
    REQUIRE(certificate.has_value());
    files.write(files.settings.certificate_file, certificate->bytes());
    auto const loaded = load_noise_credentials(files.settings, 150);
    REQUIRE(loaded.has_value());
    CHECK(loaded->static_key.public_key() == provider->public_key());

    // With a certificate, a missing static key is not made again: a new key
    // could never match it.
    std::filesystem::remove(files.settings.static_key_file);
    auto const missing = load_noise_credentials(files.settings, 150);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find(files.settings.static_key_file) != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(files.settings.static_key_file));
}

TEST_CASE("Noise refuses missing malformed and tampered offline certificates", "[sv2 noise]") {
    credential_files files;
    auto const provider = fixed_key(1);
    auto const authority = fixed_key(2);
    files.write(files.settings.static_key_file, provider.secret());
    auto const certificate = noise_certificate::sign(authority.secret(), provider.public_key(), 100, 200);
    REQUIRE(certificate.has_value());
    files.offline(*certificate, authority.public_key());
    std::filesystem::remove(files.settings.certificate_file);
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    for (auto const size : {size_t{73}, size_t{75}}) {
        auto bytes = to_chunk(certificate->bytes());
        bytes.resize(size);
        files.write(files.settings.certificate_file, bytes);
        CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    }
    auto bytes = certificate->bytes();
    bytes.back() ^= 1;
    files.write(files.settings.certificate_file, bytes);
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    bytes = certificate->bytes();
    bytes.front() = 1;
    files.write(files.settings.certificate_file, bytes);
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());

    std::filesystem::remove(files.settings.certificate_file);
    REQUIRE(std::filesystem::create_directory(files.settings.certificate_file));
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
    files.settings.certificate_file = "/dev/null";
    CHECK_FALSE(load_noise_credentials(files.settings, 150).has_value());
}

#else

TEST_CASE("Noise credential loading refuses unsupported file permission semantics", "[sv2 noise]") {
    node::sv2_settings settings;
    auto const refused = load_noise_credentials(settings, 100);
    REQUIRE_FALSE(refused.has_value());
    CHECK_FALSE(refused.error().empty());
}

#endif
